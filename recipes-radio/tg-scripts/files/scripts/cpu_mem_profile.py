#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Description: Track Idle CPU from top and /proc/meminfo

import argparse
import logging
import os
import re
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


def setup_profiler(output_folder):
    """
    clear previous data if any
    create folder to store collected data
    save PID to track/kill later
    """
    logging.info("setup_profiler")
    files_to_remove = []
    files_to_remove.append(output_folder + "/" + "dktop_*")
    files_to_remove.append(output_folder + "/" + "dkmem_*")
    files_to_remove.append(output_folder + "/" + "CURRENT_PROFILER_PID")
    files_to_remove.append(output_folder + "/" + "PROFILER_RESULT1")
    files_to_remove.append(output_folder + "/" + "PROFILER_RESULT2")
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
        my_pid_file = output_folder + "/" + "CURRENT_PROFILER_PID"
        my_pid = os.getpid()
        logging.info("pid of this script: %s" % (my_pid))
        command = "echo %s > %s" % (my_pid, my_pid_file)
        os.system(command)
    except Exception as err:
        logging.error("error in setup_profiler %s" % (err))


def start_profiler(output_folder, profile_duration, time_between_polls):
    """
    periodically collect data
    record highs and lows
    """
    logging.info("setup_profiler")
    number_of_files = int(profile_duration / time_between_polls)
    count = 0

    while count <= number_of_files:
        count = count + 1
        current_time = get_current_time()
        print("profiler count = %s" % (count))
        try:
            # logging.debug('TOP')
            file_name = output_folder + "/" + "dktop_" + current_time + ".txt"
            command = "top -b -n 1 -w 120 | head -16 > %s" % (file_name)
            os.system(command)
        except Exception as err:
            logging.error("error in start_profiler : top %s" % (err))
            logging.debug("count was = %s" % (count))
            logging.debug("file_name was = %s" % (file_name))
        try:
            # logging.debug('MEMINFO')
            file_name = output_folder + "/" + "dkmem_" + current_time + ".txt"
            command = "cat /proc/meminfo > %s" % (file_name)
            os.system(command)
        except Exception as err:
            logging.error("error in start_profiler : meminfo %s" % (err))

        time.sleep(time_between_polls)


def stop_profiler(output_folder):
    """
    kill the profiler using the saved pid
    """
    logging.info("stop_profiler")
    try:
        my_pid_file = output_folder + "/" + "CURRENT_PROFILER_PID"
        command = "kill $(cat %s)" % (my_pid_file)
        os.system(command)
        logging.info("expected min idle_metric = %s" % (min_idle_metric))
        logging.info("expected minimum memory_free = %s" % (minimum_memory_free))
        logging.info("expected minimum low_free = %s" % (minimum_low_free))
    except Exception as err:
        logging.error("error in stop_profiler %s" % (err))

    cmd = "top -b -n 1 -w 120"
    os.system(cmd)


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


def process_top_data(
    output_folder, time_between_polls, min_idle_metric, moving_average_window
):
    """
    process collected data at the end of the test and determine a pass or FAIL
    # '%Cpu(s):  1.3 us,  1.7 sy,  0.0 ni, 97.0 id,  0.0 wa,  0.0 hi,  0.1 si,  0.\n'
    """
    logging.info("process_top_data")
    result = False
    file_list = os.popen("ls %s|grep dktop_" % (output_folder)).read().split("\n")
    file_list.remove("")
    idle_cpu_values = []
    try:
        assert len(file_list) > 0, "no files found in %s - was profiler run?" % (
            output_folder
        )
        # fetch idle cpu values
        for file0 in file_list:
            file0 = output_folder + "/" + file0
            try:
                with open(file0, "r") as f:
                    line3 = f.readlines()[2]  # Just us summary line.
                    is_match = re.search("\d+(.)\d+ id", line3)
                    if is_match:
                        temp = is_match.group(0)
                        idle_cpu_values.append(float(temp.split()[0]))
                    else:
                        logging.error("match error - please review!")
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
        idle_metric = min_idle_metric
        # Hack to stop profiling if this is a traffic generator
        not_traffic_generator = os.path.isfile("/etc/watchdog.d/progress_monit.sh")
        if min(averaged_data) < idle_metric and not_traffic_generator:
            logging.error("failed by the profiler")
            results_file = output_folder + "/" + "PROFILER_RESULT1"
            command = "echo FAIL > %s" % (results_file)
            logging.info(command)
            os.system(command)
        else:
            logging.info("passed by the profiler")
            results_file = output_folder + "/" + "PROFILER_RESULT1"
            command = "echo PASS_IDLE_CPU > %s" % (results_file)
            logging.info(command)
            os.system(command)
            result = True
    except AssertionError as err:
        logging.error(err)
        logging.debug("#files collected: ", len(file_list))
        logging.debug("idle_cpu_values: ", idle_cpu_values)
        ls_output = os.popen("ls %s" % (output_folder))
        logging.debug("ls_output: \n", ls_output)
    except Exception as err:
        logging.error(err)
        logging.debug("#files collected: ", len(file_list))
        logging.debug("idle_cpu_values: ", idle_cpu_values)
        # logging.debug('averaged_data: ', averaged_data)
    logging.info("process_top_data completed!")
    return result


def process_meminfo_data_Marvell(output_folder, minimum_memory_free, minimum_low_free):
    """
    process collected meminfo data
    cat /proc/meminfo
    MemTotal:        2067504 kB
    MemFree:         1650084 kB
    MemAvailable:    1705960 kB
    :
    LowTotal:         756784 kB
    LowFree:          513956 kB
    :
    :
    """
    logging.info("process_meminfo_data_marvell")
    result = False
    file_list = os.popen("ls %s|grep dkmem_" % (output_folder)).read().split("\n")
    file_list.remove("")
    memory_free = []
    low_free = []
    try:
        assert len(file_list) > 0, "no files found in %s - was profiler run?" % (
            output_folder
        )
        # fetch idle cpu values
        for file0 in file_list:
            file0 = output_folder + "/" + file0
            try:
                with open(file0, "r") as f:
                    for line in f:
                        if line.startswith("MemFree"):
                            temp = line.split(":")[1]
                            is_match = re.search("(\s+)(\d+)( kB)", temp)
                            if is_match:
                                memory_free.append(int(is_match.group(2)))
                        if line.startswith("LowFree"):
                            temp = line.split(":")[1]
                            is_match = re.search("(\s+)(\d+)( kB)", temp)
                            if is_match:
                                low_free.append(int(is_match.group(2)))
            except Exception as err:
                logging.error("error: %s, file: %s" % (err, file0))
                with open(file0, "r") as f:
                    print(f.readlines())
        computed_memory_free_minimum = min(memory_free)
        computed_low_free_minimum = min(low_free)
        logging.info("computed minimum MemFree: %s" % (computed_memory_free_minimum))
        logging.info("computed minimum LowFree: %s" % (computed_low_free_minimum))
        if (computed_memory_free_minimum < minimum_memory_free) or (
            computed_low_free_minimum < minimum_low_free
        ):
            logging.error("failed by the profiler")
            results_file = output_folder + "/" + "PROFILER_RESULT2"
            command = "echo FAIL > %s" % (results_file)
            logging.info(command)
            os.system(command)
            logging.info("memory_free values:")
            logging.info(memory_free)
            logging.info("low_free values:")
            logging.info(low_free)
        else:
            logging.info("passed by the profiler")
            results_file = output_folder + "/" + "PROFILER_RESULT2"
            command = "echo PASS_MIN_MEMORY > %s" % (results_file)
            logging.info(command)
            os.system(command)
            result = True
    except AssertionError as err:
        logging.error(err)
        logging.debug("#files collected: ", len(file_list))
        logging.debug("low_free: ", low_free)
        logging.debug("mem_free: ", memory_free)
        ls_output = os.popen("ls %s" % (output_folder))
        logging.debug("ls_output: \n", ls_output)
    except Exception as err:
        logging.error(err)
        logging.debug("#files collected: ", len(file_list))
        logging.debug("low_free: ", low_free)
        logging.debug("mem_free: ", memory_free)
    logging.info("process_meminfo_data_marvell() completed!")
    return result


def process_meminfo_data_Jaguar(output_folder, minimum_memory_free):
    """
    process collected meminfo data
    cat /proc/meminfo
    MemTotal:        2067504 kB
    MemFree:         1650084 kB
    MemAvailable:    1705960 kB
    :
    LowTotal:         756784 kB
    :
    :
    """
    logging.info("process_meminfo_data_jaguar")
    result = False
    file_list = os.popen("ls %s|grep dkmem_" % (output_folder)).read().split("\n")
    file_list.remove("")
    memory_free = []
    try:
        assert len(file_list) > 0, "no files found in %s - was profiler run?" % (
            output_folder
        )
        # fetch idle cpu values
        for file0 in file_list:
            file0 = output_folder + "/" + file0
            try:
                with open(file0, "r") as f:
                    for line in f:
                        if line.startswith("MemFree"):
                            temp = line.split(":")[1]
                            is_match = re.search("(\s+)(\d+)( kB)", temp)
                            if is_match:
                                memory_free.append(int(is_match.group(2)))
            except Exception as err:
                logging.error("error: %s, file: %s" % (err, file0))
                with open(file0, "r") as f:
                    print(f.readlines())
        computed_memory_free_minimum = min(memory_free)
        logging.info("computed minimum MemFree: %s" % (computed_memory_free_minimum))

        if computed_memory_free_minimum < minimum_memory_free:
            logging.error("failed by the profiler")
            results_file = output_folder + "/" + "PROFILER_RESULT2"
            command = "echo FAIL > %s" % (results_file)
            logging.info(command)
            os.system(command)
            logging.info("memory_free values:")
            logging.info(memory_free)
        else:
            logging.info("passed by the profiler")
            results_file = output_folder + "/" + "PROFILER_RESULT2"
            command = "echo PASS_MIN_MEMORY > %s" % (results_file)
            logging.info(command)
            os.system(command)
            result = True
    except AssertionError as err:
        logging.error(err)
        logging.debug("#files collected: ", len(file_list))
        logging.debug("mem_free: ", memory_free)
        ls_output = os.popen("ls %s" % (output_folder))
        logging.debug("ls_output: \n", ls_output)
    except Exception as err:
        logging.error(err)
        logging.debug("#files collected: ", len(file_list))
        logging.debug("mem_free: ", memory_free)
    logging.info("process_meminfo_data_jaguar() completed!")
    return result


def start_option_log(log_file_prefix):
    log_file_name = log_file_prefix + "/cpu_mem_profile_start.log"
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
    log_file_name = log_file_prefix + "/cpu_mem_profile_stop.log"
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
    dot_log_files = "cpu_mem_profile*.log"

    if folder_for_tas == output_folder:
        # just copy the .log file in /tmp to /tmp/custom_logs
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

    source_files1 = output_folder + "/dk*"
    source_files2 = output_folder + "/PROFILER_RESULT*"
    source_files3 = output_folder + "/CURRENT_PROFILER_PID"
    source_files4 = output_folder + "/cpu_mem*"

    command = "cp %s %s" % (source_files0, destination)
    os.system(command)

    print("move and/copy the generated files to /tmp/custom_logs")
    command = "mv %s %s" % (source_files1, destination)
    os.system(command)
    command = "cp %s %s" % (source_files2, destination)
    os.system(command)
    command = "cp %s %s" % (source_files3, destination)
    os.system(command)
    command = "cp %s %s" % (source_files4, destination)
    os.system(command)


if __name__ == "__main__":

    script_return_value = -1

    parser = argparse.ArgumentParser(description=("Profiler Parameters"))

    parser.add_argument("-t", "--time", default=7200, help="how long (sec)?", type=int)

    parser.add_argument(
        "--time_between_polls", default=2, help="how often to poll (in sec)", type=float
    )

    parser.add_argument(
        "--min_idle_metric",
        default=20,
        help="min idle cpu percent on marvell",
        type=float,
    )

    parser.add_argument(
        "--min_memory_free",
        default=200,
        help="min free memory size in kB as seen in /proc/meminfo",
        type=float,
    )

    parser.add_argument(
        "--min_low_free",
        default=100,
        help="min low memory size in kB as seen in /proc/meminfo (Marvell)",
        type=float,
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
    minimum_memory_free = args.min_idle_metric  # kB
    minimum_low_free = args.min_memory_free  # kB
    moving_average_window = time_between_polls * 3  # secs
    logging.info(args)
    result = False

    if args.data:
        log_file_prefix = "/data"
        output_folder = "/data/custom_logs"
    else:
        log_file_prefix = "/tmp"
        output_folder = "/tmp/custom_logs"

    if args.start:
        start_option_log(log_file_prefix)
        logging.info(args)
        setup_profiler(output_folder)
        start_profiler(output_folder, profile_duration, time_between_polls)
    else:
        if args.stop:
            stop_option_log(log_file_prefix)
            logging.info(args)
            stop_profiler(output_folder)
            result1 = process_top_data(
                output_folder,
                time_between_polls,
                min_idle_metric,
                moving_average_window,
            )
            digital_board_type = os.popen("get_hw_info HW_BOARD_ID").read().strip("\n")

            if digital_board_type == "MVL_ARMADA39X_P":
                result2 = process_meminfo_data_Marvell(
                    output_folder, minimum_memory_free, minimum_low_free
                )
            elif digital_board_type == "NXP_LS1048A_JAGUAR":
                result2 = process_meminfo_data_Jaguar(
                    output_folder, minimum_memory_free
                )
            else:
                result2 = True  # future

            if result1 and result2:
                logging.info("==PASS==")
                script_return_value = 0
            move_logs(output_folder)  # MUST BE THE LAST FUNCTION CALL
            sys.exit(script_return_value)
        else:
            logging.error("must select --start or --stop")
            sys.exit(script_return_value)
