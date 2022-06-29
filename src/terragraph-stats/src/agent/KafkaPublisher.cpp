/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "KafkaPublisher.h"

#include <cppkafka/message.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <future>
#include <gflags/gflags.h>

#include "e2e/common/Consts.h"
#include "e2e/common/JsonUtils.h"
#include "stats/common/StatInfo.h"

DEFINE_string(
    kafka_event_cache_file, "/tmp/kafka_events.json", "Event cache file");

DEFINE_int32(
    kafka_events_retry_interval_s,
    30,
    "Retransmit dropped events to Kafka at this interval (in seconds)");

using apache::thrift::detail::TEnumMapFactory;
using cppkafka::Configuration;
using cppkafka::Topic;
using cppkafka::MessageBuilder;

namespace {
// microseconds per second
const int kUsPerS{1000000};
} // namespace

namespace facebook {
namespace terragraph {
namespace stats {

KafkaPublisher::KafkaPublisher(
    fbzmq::Context& context,
    const std::string& macAddr,
    const thrift::StatsAgentParams& statsAgentParams)
    : BasePublisher(context, macAddr, "KafkaPublisher", statsAgentParams),
      nmsDefaultReportInterval_(std::chrono::seconds(
          statsAgentParams.publisherParams.defaultStatsInterval)),
      nmsHighFrequencyReportInterval_(std::chrono::seconds(
          statsAgentParams.publisherParams.highFrequencyStatsInterval)) {
  // Load cached events
  loadEvents();

  // Initialize Kafka producer
  kafkaInit(statsAgentParams.endpointParams.kafkaParams_ref().value());

  // Create timers for periodically pushing stats/events to Kafka
  const bool makePeriodic = true;
  nmsDefaultReportTimer_ = fbzmq::ZmqTimeout::make(this, [&]() noexcept {
    pushQueuedStats(curValuesLF_, prevValuesLF_, kafkaTopics_.statsTopic);
  });
  nmsDefaultReportTimer_->scheduleTimeout(
      nmsDefaultReportInterval_, makePeriodic);
  nmsHighFrequencyReportTimer_ = fbzmq::ZmqTimeout::make(this, [&]() noexcept {
    pushQueuedStats(curValuesHF_, prevValuesHF_, kafkaTopics_.hfStatsTopic);
  });
  nmsHighFrequencyReportTimer_->scheduleTimeout(
      nmsHighFrequencyReportInterval_, makePeriodic);
  kafkaDroppedEventsTimer_ = fbzmq::ZmqTimeout::make(this, [&]() noexcept {
    pushDroppedEvents();
  });
  kafkaDroppedEventsTimer_->scheduleTimeout(
      std::chrono::seconds(FLAGS_kafka_events_retry_interval_s),
      makePeriodic);
}

KafkaPublisher::~KafkaPublisher() {
  if (kafkaFlushThread_.joinable()) {
    kafkaFlushThreadStop_ = true;
    kafkaFlushThread_.join();
  }
}

void
KafkaPublisher::kafkaInit(const thrift::KafkaParams& kafkaParams) {
  // Create Kafka configuration for all topics
  LOG(INFO) << "Kafka publishing enabled with broker list: "
            << kafkaParams.config.brokerEndpointList;
  kafkaConfig_ = {
      {"metadata.broker.list", kafkaParams.config.brokerEndpointList},
      {"queue.buffering.max.ms", kafkaParams.config.queueBufferingMaxMs},
      {
        "queue.buffering.max.messages",
        kafkaParams.config.queueBufferingMaxMessages
      },
      {
        "queue.buffering.max.kbytes",
        kafkaParams.config.queueBufferingMaxKbytes
      },
      {"batch.num.messages", kafkaParams.config.batchNumMessages},
      {"enable.idempotence", kafkaParams.config.enableIdempotence},
      {"compression.codec", kafkaParams.config.compressionCodec},
      {"compression.level", kafkaParams.config.compressionLevel},
      {"message.timeout.ms", kafkaParams.config.messageTimeoutMs},
  };
  kafkaTopics_ = kafkaParams.topics;
  kafkaMaxBufferSize_ =
      (size_t)std::max(0, kafkaParams.config.queueBufferingMaxMessages);

  // Create Kafka producer for all topics
  kafkaProducer_.reset(
      new cppkafka::BufferedProducer<std::string>(kafkaConfig_));

  // Set callbacks for tracking unacknowledged event messages
  kafkaProducer_->set_produce_success_callback(
      [&](const cppkafka::Message& msg) {
    if (msg.get_topic() != kafkaTopics_.eventsTopic) {
      return;  // only care about events
    }

    // Successfully sent event - remove from queues
    if (eventsInFlight_.erase(msg.get_payload())) {
      VLOG(4) << "Event acked, removing from 'in-flight' queue...";
    } else if (eventsDropped_.erase(msg.get_payload())) {
      LOG(ERROR) << "Transmitted event found in 'dropped' queue: "
                 << msg.get_payload();
    } else {
      LOG(ERROR) << "Transmitted event not found in queues: "
                 << msg.get_payload();
    }
  });
  kafkaProducer_->set_produce_termination_callback(
      [&](const cppkafka::Message& msg) {
    if (msg.get_topic() != kafkaTopics_.eventsTopic) {
      return;  // only care about events
    }

    if (eventsInFlight_.erase(msg.get_payload())) {
      VLOG(4) << "Event sending failed, moving to 'dropped' queue...";
      eventsDropped_.insert(msg.get_payload());
    } else {
      LOG(ERROR) << "Dropped event not found in queues: " << msg.get_payload();
    }
  });
  kafkaProducer_->set_queue_full_notification(
      cppkafka::BufferedProducer<std::string>
          ::QueueFullNotification::OncePerMessage);
  kafkaProducer_->set_queue_full_callback(
      [&](const cppkafka::MessageBuilder& /*builder*/) {
    VLOG(2) << "Kafka producer queue full. Messages in buffer: "
            << kafkaProducer_->get_buffer_size();
  });

  // Initialize producer flushing thread (NOTE: need a better way to interrupt?)
  // TODO: async_flush() actually blocks on an underlying poll() loop when the
  //       queue is very full. So we still need to check the current buffer size
  //       before attempting to produce anything.
  kafkaFlushThread_ = std::thread([&, kafkaParams]() {
    size_t totalProduced = kafkaProducer_->get_total_messages_produced();
    std::chrono::milliseconds bufferInterval(
        kafkaParams.config.queueBufferingMaxMs);

    while (!kafkaFlushThreadStop_) {
      std::this_thread::sleep_for(bufferInterval);

      // Log # acked messages
      size_t n = kafkaProducer_->get_total_messages_produced();
      if (n - totalProduced > 0) {
        VLOG(2) << "Kafka producer acks received for " << (n - totalProduced)
                << " message(s)";
      }
      totalProduced = n;

      // Flush messages, do not wait for acks
      kafkaProducer_->async_flush();

      // Log # pending acks and buffered messages
      size_t pendingAcks = kafkaProducer_->get_pending_acks();
      if (pendingAcks > 0) {
        VLOG(3) << "Kafka producer has " << pendingAcks << " pending ack(s)";
      }
      size_t bufferSize = kafkaProducer_->get_buffer_size();
      if (bufferSize > 0) {
        VLOG(2) << "Kafka producer has " << bufferSize
                << " buffered message(s)";
      }
    }
  });
}

void
KafkaPublisher::processCountersMessage(
    const fbzmq::thrift::CounterValuesResponse& counters) noexcept {
  int64_t ts = std::time(nullptr);

  // Create datapoints from counters, add to queue
  int bufferFullDroppedKeys = 0;
  for (const auto& kv : counters.counters_ref().value()) {
    const StatInfo info(kv.first);

    // Check if blacklisted
    if (isBlacklisted(info.key)) {
      VLOG(4) << "Dropped blacklisted counter '" << info.key << "'";
      continue;
    }

    // Check max queue size
    size_t totalQueueSize = curValuesLF_.size() + curValuesHF_.size();
    if (totalQueueSize >= statsBufferSize_ && !curValuesLF_.count(kv.first)) {
      bufferFullDroppedKeys++;
      continue;
    }

    // Check if whitelisted as a high frequency stat
    bool isHighFreqKey = isWhitelisted(info.key);

    // Normalize the reported timestamp
    fbzmq::thrift::Counter counter = kv.second;
    counter.timestamp_ref() =
        getUnixUsTimestamp(kv.second.timestamp_ref().value(), ts) / kUsPerS;

    // Counters require computing the difference for the time period
    if (kv.second.valueType_ref().value() ==
        fbzmq::thrift::CounterValueType::COUNTER) {
      // keep the oldest value in prevValue_, then update curValue_ everytime.
      auto pIt = prevValuesLF_.find(kv.first);
      if (pIt == prevValuesLF_.end()) {
        // no previous value, set for the first time
        prevValuesLF_[kv.first] = counter;
      }
      if (isHighFreqKey) {
        auto pIt = prevValuesHF_.find(kv.first);
        if (pIt == prevValuesHF_.end()) {
          prevValuesHF_[kv.first] = counter;
        }
      }
    }

    // Always update current value, even if same as prevValue, since we'll
    // flip prevValues = curValues on iterations
    curValuesLF_[kv.first] = counter;
    if (isHighFreqKey) {
      curValuesHF_[kv.first] = counter;
    }
  }

  if (bufferFullDroppedKeys > 0) {
    LOG(WARNING) << "Stats buffer full (" << statsBufferSize_
                 << " keys), dropped " << bufferFullDroppedKeys << " new keys!";
  }
}

void
KafkaPublisher::processEventLogMessage(
    const fbzmq::thrift::EventLog& eventLog) noexcept {
  // We only expect single-sample logs
  if (eventLog.samples_ref().value().size() != 1) {
    return;
  }

  size_t kafkaBufferSize = kafkaProducer_->get_buffer_size();

  // Determine the topic based on fbzmq::thrift::EventLog category
  std::string topic;
  std::string payload;
  if (eventLog.category_ref().value() == E2EConsts::kEventCategory) {
    // Check current queue sizes
    if (eventsInFlight_.size() + eventsDropped_.size() >= eventsBufferSize_) {
      LOG(ERROR) << "Events buffer full (>=" << eventsBufferSize_
                << " events), dropping new event";
      return;
    }

    topic = kafkaTopics_.eventsTopic;

    // Deserialize event from JSON
    auto maybeEvent = parseTerragraphEventLog(eventLog);

    // Re-serialize event to JSON
    payload = JsonUtils::serializeToJson<thrift::Event>(maybeEvent.value());

    // Track this event
    eventsInFlight_.insert(payload);

    if (kafkaBufferSize >= kafkaMaxBufferSize_) {
      LOG(ERROR) << "Kafka producer buffer full (" << kafkaBufferSize << " >= "
                 << kafkaMaxBufferSize_ << " messages), queueing new event";
      return;
    }

    VLOG(2) << "Producing event to Kafka topic '" << topic << "' ["
            << folly::get_default(
                   TEnumMapFactory<thrift::EventId>::makeValuesToNamesMap(),
                   maybeEvent->eventId,
                   "UNKNOWN")
            << "]";
  } else {
    if (eventLog.category_ref().value() == E2EConsts::kEventScanResultCategory)
    {
      topic = kafkaTopics_.scanResultsTopic;
    } else if (eventLog.category_ref().value() ==
        E2EConsts::kEventIperfResultCategory) {
      topic = kafkaTopics_.iperfResultsTopic;
    } else if (eventLog.category_ref().value() ==
        E2EConsts::kEventPingResultCategory) {
      topic = kafkaTopics_.pingResultsTopic;
    } else {
      // Skip any events generated by OpenR (in category "perfpipe_aquaman")
      return;
    }

    if (kafkaBufferSize >= kafkaMaxBufferSize_) {
      LOG(ERROR) << "Kafka producer buffer full (" << kafkaBufferSize << " >= "
                 << kafkaMaxBufferSize_
                 << " messages), dropping new data for topic '" << topic << "'";
      return;
    }

    // Use EventLog payload directly
    payload = eventLog.samples_ref().value()[0];

    VLOG(2) << "Producing data to Kafka topic '" << topic << "'";
  }

  // Send to Kafka
  kafkaProducer_->add_message(MessageBuilder(topic).payload(payload));
}

void
KafkaPublisher::pushDroppedEvents() {
  if (eventsDropped_.empty()) {
    return;
  }

  size_t kafkaBufferSize = kafkaProducer_->get_buffer_size();
  if (kafkaBufferSize >= kafkaMaxBufferSize_) {
    LOG(ERROR) << "Kafka producer buffer full (" << kafkaBufferSize << " >= "
               << kafkaMaxBufferSize_ << " messages), not retransmitting "
               << eventsDropped_.size() << " events(s)";
    return;
  }

  VLOG(2) << "Retransmitting " << eventsDropped_.size() << " event(s)...";
  auto eventsDropped = std::move(eventsDropped_);
  eventsDropped_ = {};
  for (const std::string& payload : eventsDropped) {
    eventsInFlight_.insert(payload);
    kafkaProducer_->add_message(
        MessageBuilder(kafkaTopics_.eventsTopic).payload(payload));
  }
}

void
KafkaPublisher::pushQueuedStats(
    KafkaPublisher::StatsMap& curValues,
    KafkaPublisher::StatsMap& prevValues,
    const std::string& statsTopic) {
  // Skip empty stats queue
  if (curValues.empty()) {
    return;
  }

  size_t kafkaBufferSize = kafkaProducer_->get_buffer_size();
  if (kafkaBufferSize >= kafkaMaxBufferSize_) {
    LOG(ERROR) << "Kafka producer buffer full (" << kafkaBufferSize << " >= "
               << kafkaMaxBufferSize_ << " messages), dropping "
               << curValues.size() << " new stat(s)";
  } else {
    size_t produceCount = 0;
    for (const auto& counterKv : curValues) {
      int64_t ts = counterKv.second.timestamp_ref().value();
      bool isCounter =
          counterKv.second.valueType_ref() ==
              fbzmq::thrift::CounterValueType::COUNTER;

      if (isCounter && convertToRate_) {
        // For counters, calculate rate from prev value
        std::optional<double> maybeRate =
            getCounterRate(counterKv.first, counterKv.second, prevValues);
        if (publishValueWithRate_) {
          // Publish raw value and rate as separate keys
          publishStat(
              statsTopic, counterKv.first, ts,
              counterKv.second.value_ref().value(), true);
          produceCount++;
          if (maybeRate) {
            publishStat(
                statsTopic, counterKv.first + ".rate", ts, *maybeRate, false);
            produceCount++;
          }
        } else {
          // Publish rate only (if valid)
          if (maybeRate) {
            publishStat(statsTopic, counterKv.first, ts, *maybeRate, true);
            produceCount++;
          }
        }
      } else {
        // Push raw value
        publishStat(
            statsTopic, counterKv.first, ts,
            counterKv.second.value_ref().value(), isCounter);
        produceCount++;
      }
    }

    VLOG(2) << "Produced " << produceCount << " stat(s) to Kafka topic '"
            << statsTopic << "'";
  }

  // Flip current values to previous values
  prevValues = std::move(curValues);
  // Wipe current values
  curValues.clear();
}

void
KafkaPublisher::publishStat(
    const std::string& statsTopic,
    const std::string& key,
    int64_t timestamp,
    double val,
    bool isCounter) {
  // Create stat key
  const StatInfo info(key, macAddr_);
  thrift::AggrStat stat;
  stat.timestamp = timestamp;
  stat.key = info.key;
  stat.isCounter = isCounter;
  stat.value = val;
  stat.entity_ref() = info.entity;

  // Produce Kafka message
  const std::string statJson =
      JsonUtils::serializeToJson<thrift::AggrStat>(stat);
  kafkaProducer_->add_message(
      MessageBuilder(statsTopic).key(stat.key).payload(statJson));
  VLOG(6) << "Produced to '" << statsTopic << "': " << statJson;
}

void
KafkaPublisher::cacheEvents() {
  // Copy event queues to a single EventLog
  thrift::EventLog eventLog;
  std::copy(
      eventsInFlight_.begin(),
      eventsInFlight_.end(),
      std::back_inserter(eventLog.events));
  std::copy(
      eventsDropped_.begin(),
      eventsDropped_.end(),
      std::back_inserter(eventLog.events));
  if (eventLog.events.empty()) {
    VLOG(2) << "No events to cache";
    return;
  }

  // Write EventLog to file
  VLOG(2) << "Caching " << eventLog.events.size() << " event(s)...";
  std::string contents = JsonUtils::serializeToJson(eventLog);
  try {
    folly::writeFileAtomic(FLAGS_kafka_event_cache_file, contents);
  } catch (const std::exception&) {
    LOG(ERROR) << "Could not write events to " << FLAGS_kafka_event_cache_file;
  }
}

void
KafkaPublisher::loadEvents() {
  // Deserialize cached events to EventLog
  thrift::EventLog eventLog;
  try {
    std::string contents;
    auto jsonSerializer = apache::thrift::SimpleJSONSerializer();
    if (folly::readFile(FLAGS_kafka_event_cache_file.c_str(), contents)) {
      jsonSerializer.deserialize(contents, eventLog);

      // Delete file
      std::remove(FLAGS_kafka_event_cache_file.c_str());
    }
  } catch (const std::exception&) {
    LOG(ERROR) << "Could not read events from " << FLAGS_kafka_event_cache_file;
    return;
  }

  // Re-queue the loaded events
  if (eventLog.events.empty()) {
    VLOG(2) << "No events found in cache";
    return;
  }
  VLOG(2) << "Loaded " << eventLog.events.size() << " event(s) from cache...";
  std::copy(
      eventLog.events.begin(),
      eventLog.events.end(),
      std::inserter(eventsDropped_, eventsDropped_.end()));
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
