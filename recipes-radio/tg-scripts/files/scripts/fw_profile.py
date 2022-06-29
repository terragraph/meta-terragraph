#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Description: track Broadcom Idle CPU from dmesg

import argparse
import logging
import os
import sys
import time
from datetime import datetime
from operator import sub


UTC_TIME_FORMAT1 = "%Y-%m-%dT%H:%M:%S+0000"


class NullHandler(logging.Handler):
    """Placeholder handler."""

    def emit(self, record):
        pass


logging.getLogger(__name__).addHandler(NullHandler())

logger = logging.getLogger()

system_log_level = logging.DEBUG  # severity = max(system_log_level, handler_log_level)
logger.setLevel(system_log_level)  # use this as control lever
console_log_format = logging.Formatter(
    "%(asctime)s | %(name)s | %(filename)s : %(lineno)d | %(levelname)s | %(message)s"
)
ch = logging.StreamHandler()
handler_log_level = logging.INFO
ch.setLevel(handler_log_level)
ch.setFormatter(console_log_format)
logger.addHandler(ch)


def get_current_time():
    """
    return the current local time
    """
    # return time.strftime(UTC_TIME_FORMAT1)
    return str(datetime.now().strftime(UTC_TIME_FORMAT1))


def setup_broadcom_profiler(output_folder):
    """
    clear previous data if any
    create folder to store collected data
    save PID to track/kill later
    """
    logging.info("setup_profiler")
    files_to_remove = []
    files_to_remove.append(output_folder + "/" + "dk_broadcom_idle*")

    try:
        logging.info("clean previous data if any")
        for file0 in files_to_remove:
            command = "rm -rf %s" % (file0)
            logging.info(command)
            os.system(command)
        logging.info("create directory %s" % (output_folder))  # make TAS do it!
        command = "mkdir -p %s" % (output_folder)
        os.system(command)
        # os.mkdir('%s' % (output_folder))
        my_pid_file = output_folder + "/" + "BROADCOM_PROFILER_PID"
        my_pid = os.getpid()
        logging.info("pid of this script: %s" % (my_pid))
        command = "echo %s > %s" % (my_pid, my_pid_file)
        os.system(command)
    except Exception as err:
        logging.error("error in setup_profiler %s" % (err))


def start_broadcom_profiler(output_folder, profile_duration, time_between_polls):
    """
    periodically collect data
    dmesg | grep 'CPU Idle' | tail -1
    """
    logging.info("setup_profiler")
    number_of_polls = int(profile_duration / time_between_polls)
    count = 0
    current_time = get_current_time()
    file_name = output_folder + "/" + "dk_broadcom_idle_" + current_time + ".txt"
    while count <= number_of_polls:
        count = count + 1
        print("profiler count = %s" % (count))
        try:
            # logging.debug('TOP')
            command = 'dmesg | grep "CPU Idle" | tail -1 >> %s' % (file_name)
            os.system(command)
        except Exception as err:
            logging.error("error in start_profiler : top %s" % (err))
            logging.debug("count was = %s" % (count))

        time.sleep(time_between_polls)


def stop_broadcom_profiler(output_folder):
    """
    kill the profiler using the saved pid
    """
    logging.info("stop_profiler")
    return_val = False
    try:
        my_pid = os.getpid()
        logging.info("pid of this stop script: %s" % (my_pid))
        my_pid_file = output_folder + "/" + "BROADCOM_PROFILER_PID"
        command = "kill `cat %s`" % (my_pid_file)  # was using kill -9
        logging.info(command)
        os.system(command)
        logging.info("expected min idle_metric = %s" % (min_idle_metric))
    except Exception as err:
        logging.error("error in stop_profiler %s" % (err))
    return_val = True
    return return_val


def moving_average(data, n=6):
    try:
        averaged_data = []
        averaged_data_fin = []
        length = len(data)
        averaged_data = [sum(data[0 : x + 1]) for x in range(0, length)]
        averaged_data[n:] = map(sub, averaged_data[n:], averaged_data[:-n])
        averaged_data = [float(i) for i in averaged_data]
        for i in range(n - 1, length):
            averaged_data_fin.append(averaged_data[i] / n)
        return averaged_data_fin
    except Exception as err:
        print("error in moving_average")
        print(err)  # ignore


def process_broadcom_idle_stats(
    output_folder, time_between_polls, min_idle_metric, moving_average_window
):
    """
    process collected data at the end of the test and determine a pass or FAIL
    #'%Cpu(s):  1.3 us,  1.7 sy,  0.0 ni, 97.0 id,  0.0 wa,  0.0 hi,  0.1 si,  0.\n'
    """
    logging.info("process_broadcom_idle_data")
    result = False
    file_list = (
        os.popen("ls %s|grep dk_broadcom_idle*" % (output_folder)).read().split("\n")
    )
    file_list.remove("")
    idle_cpu_values = []
    try:
        assert len(file_list) == 1, "no file found in %s - was profiler run?" % (
            output_folder
        )
        # fetch idle cpu values
        file0 = output_folder + "/" + file_list[0]
        try:
            with open(file0, "r") as f:
                for line in f:
                    temp = float(line.split()[-1].replace("%", ""))
                    idle_cpu_values.append(temp)
        except Exception as err:
            logging.error("error: %s, file: %s" % (err, file0))
            with open(file0, "r") as f:
                print(f.readlines())
        # compute moving average
        number_of_samples = int(moving_average_window / time_between_polls)
        averaged_data = moving_average(idle_cpu_values, number_of_samples)
        logging.info("minimum moving average idle: %s" % (min(averaged_data)))
        logging.info("maximum moving average idle: %s" % (max(averaged_data)))
        logging.info("minimum instantaneous idle: %s" % (min(idle_cpu_values)))
        logging.info("maximum instantaneous idle: %s" % (max(idle_cpu_values)))
        if min(averaged_data) < min_idle_metric:
            logging.error("failed by the profiler")
            results_file = output_folder + "/" + "BROADCOM_PROFILER_RESULT"
            command = "echo FAIL > %s" % (results_file)
            logging.info(command)
            os.system(command)
        else:
            logging.info("passed by the profiler")
            results_file = output_folder + "/" + "BROADCOM_PROFILER_RESULT"
            command = "echo PASS_BROADCOM_IDLE_CPU > %s" % (results_file)
            logging.info(command)
            os.system(command)
            result = True
    except AssertionError as err:
        logging.error(err)
        logging.debug("idle_cpu_values: ", idle_cpu_values)
        ls_output = os.popen("ls %s" % (output_folder))
        logging.debug("ls_output: \n", ls_output)
    except Exception as err:
        logging.error(err)
        logging.debug("idle_cpu_values: ", idle_cpu_values)
        # logging.debug('averaged_data: ', averaged_data)
    logging.info("process_broadcom_idle_stats() completed!")
    return result


def start_option_log(log_file_prefix):
    log_file_name = log_file_prefix + "/broadcom_profile_start.log"
    command = "> %s" % (log_file_name)
    logging.info(command)
    os.system(command)
    file_format = logging.Formatter(
        "%(asctime)s | %(name)s | %(filename)s : %(lineno)d | %(levelname)s | %(message)s"
    )
    fh = logging.FileHandler(log_file_name)
    file_log_level = logging.DEBUG  # max(system_log_level, file_log_level)
    fh.setLevel(file_log_level)
    fh.setFormatter(file_format)
    logger.addHandler(fh)


def stop_option_log(log_file_prefix):
    log_file_name = log_file_prefix + "/broadcom_profile_stop.log"
    command = "> %s" % (log_file_name)
    logging.info(command)
    os.system(command)
    file_format = logging.Formatter(
        "%(asctime)s | %(name)s | %(filename)s : %(lineno)d | %(levelname)s | %(message)s"
    )
    fh = logging.FileHandler(log_file_name)
    file_log_level = logging.DEBUG  # max(system_log_level, file_log_level)
    fh.setLevel(file_log_level)
    fh.setFormatter(file_format)
    logger.addHandler(fh)


def move_logs(output_folder):
    folder_for_tas = "/tmp/custom_logs"
    dot_log_files = "broadcom_profile*.log"

    if folder_for_tas == output_folder:
        # just 'copy' the .log file in /tmp to /tmp/custom_logs
        print("create directory %s" % (folder_for_tas))
        command = "mkdir -p %s" % (folder_for_tas)
        os.system(command)
        source_files0 = "/tmp/" + dot_log_files
        destination = folder_for_tas + "/"
        command = "cp %s %s" % (source_files0, destination)
        os.system(command)
        return

    # no more python logging, just using print()
    print("create directory %s" % (folder_for_tas))
    command = "mkdir -p %s" % (folder_for_tas)
    os.system(command)

    source_files0 = "/data/" + dot_log_files
    destination = folder_for_tas + "/"

    source_files1 = output_folder + "/dk_broadcom_idle*"
    source_files2 = output_folder + "/BROADCOM_PROFILER_PID"
    source_files3 = output_folder + "/broadcom_profile*"

    command = "cp %s %s" % (source_files0, destination)
    os.system(command)

    print("move and/copy the generated files to /tmp/custom_logs")
    command = "mv %s %s" % (source_files1, destination)
    os.system(command)
    command = "cp %s %s" % (source_files2, destination)
    os.system(command)
    command = "cp %s %s" % (source_files3, destination)
    os.system(command)


if __name__ == "__main__":

    script_return_value = -1

    parser = argparse.ArgumentParser(description=("Profiler Parameters"))

    parser.add_argument("-t", "--time", default=7200, help="how long (sec)?", type=int)

    parser.add_argument(
        "--min_idle_metric",
        default=15,
        help="min idle cpu percent on broadcom",
        type=float,
    )

    parser.add_argument(
        "--time_between_polls", default=2, help="how often to poll (in sec)", type=float
    )

    group1 = parser.add_mutually_exclusive_group()
    group1.add_argument("--start", help="start profiler", action="store_true")
    group1.add_argument("--stop", help="stop profiler", action="store_true")

    group2 = parser.add_mutually_exclusive_group()
    group2.add_argument("--data", help="write to /data", action="store_true")
    group2.add_argument("--tmp", help="write to /tmp", action="store_true")

    args = parser.parse_args()

    # output_folder = args.output_folder
    profile_duration = args.time
    time_between_polls = args.time_between_polls

    min_idle_metric = args.min_idle_metric  # percent
    moving_average_window = time_between_polls * 3  # secs
    logging.info(args)
    result = False

    if args.data:
        log_file_prefix = "/data"
        output_folder = "/data/custom_logs"
        logging.info("running scripts off /data")
    else:
        log_file_prefix = "/tmp"
        output_folder = "/tmp/custom_logs"
        logging.info("running scripts off /tmp")

    if args.start:
        start_option_log(log_file_prefix)
        logging.info(args)
        setup_broadcom_profiler(output_folder)
        start_broadcom_profiler(output_folder, profile_duration, time_between_polls)
    else:
        if args.stop:
            stop_option_log(log_file_prefix)
            logging.info(args)
            go_ahead = stop_broadcom_profiler(output_folder)  # ignore return value
            result1 = process_broadcom_idle_stats(
                output_folder,
                time_between_polls,
                min_idle_metric,
                moving_average_window,
            )
            if result1:
                logging.info("==PASS==")
                script_return_value = 0
            move_logs(output_folder)  # MUST BE THE LAST FUNCTION CALL
            sys.stdout.write("Writing the Exit Code for Shell $?\n")
            sys.exit(script_return_value)
        else:
            logging.error("must select --start or --stop")
            sys.exit(script_return_value)
