#!/usr/bin/env python3

import re
import numpy
import datetime
import airium
import itertools
from dataclasses import dataclass, field


def size_format(num):
    for unit in ["", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi"]:
        if abs(num) < 1024.0:
            return f"{num:g} {unit}B"
        num /= 1024.0
    return f"{num:g} YiB"


@dataclass
class benchmark_environment:
    OS: str = ""
    compiler: str = ""
    storage_account: str = ""
    azure_vm: str = ""
    azure_core_version: str = ""
    azure_storage_common_version: str = ""
    azure_storage_blobs_version: str = ""


@dataclass
class transfer_configuration:
    blob_size: int
    num_blobs: int
    concurrency: int


@dataclass
class benchmark_case:
    case_name: str
    transfer_config: transfer_configuration
    transport: str
    total_time_ms: list[int] = field(default_factory=list)


@dataclass
class benchmark_suite:
    environment = benchmark_environment()
    transports = []
    baseline_transport: str = ""
    benchmark_cases = []
    transfer_configs = []
    cases = []


def parse_log_file(filename):
    suite = benchmark_suite()
    env = benchmark_environment()

    repeat_times = None

    line_pattern = re.compile("^\[(.{23})\] \[([a-z]+)\] (.+)\n$")
    for l in open(filename):
        match_object = re.fullmatch(line_pattern, l)
        log_time = datetime.datetime.strptime(
            match_object.group(1), "%Y-%m-%d %H:%M:%S.%f")
        (log_level, log_message) = match_object.group(2, 3)

        if m := re.fullmatch("using storage account: (.+)", log_message):
            suite.environment.storage_account = m.group(1)
        elif m := re.fullmatch("OS: (.+)", log_message):
            suite.environment.OS = m.group(1)
        elif m := re.fullmatch("compiler: (.+)", log_message):
            suite.environment.compiler = m.group(1)
        elif m := re.fullmatch("azure-core-cpp version: (.+)", log_message):
            suite.environment.azure_core_version = m.group(1)
        elif m := re.fullmatch("azure-storage-common-cpp version: (.+)", log_message):
            suite.environment.azure_storage_common_version = m.group(1)
        elif m := re.fullmatch("azure-storage-blobs-cpp version: (.+)", log_message):
            suite.environment.azure_storage_blobs_version = m.group(1)
        elif m := re.fullmatch("transports: (.+)", log_message):
            suite.transports = [i.strip() for i in m.group(1).split(",")]
        elif m := re.fullmatch("baseline transport: (.+)", log_message):
            suite.baseline_transport = m.group(1)
            suite.transports.remove(suite.baseline_transport)
            suite.transports.insert(0, suite.baseline_transport)
        elif m := re.fullmatch("benchmark cases: (.+)", log_message):
            suite.benchmark_cases = [i.strip() for i in m.group(1).split(",")]
        elif m := re.fullmatch("tr?ansfer config ([0-9]+): blob size: ([0-9]+) bytes, number of blobs: ([0-9]+), concurrency: ([0-9]+)", log_message):
            suite.transfer_configs.append(transfer_configuration(
                int(m.group(2)), int(m.group(3)), int(m.group(4))))
            assert(len(suite.transfer_configs) == int(m.group(1)))
        elif m := re.fullmatch("repeat times: ([0-9]+)", log_message):
            repeat_times = int(m.group(1))
            for c in suite.benchmark_cases:
                for tc in suite.transfer_configs:
                    for t in suite.transports:
                        suite.cases.append(benchmark_case(c, tc, t))
        elif m := re.fullmatch("(.+) used ([0-9]+)ms to (.+) ([0-9]+) ([0-9]+)-byte blobs with ([0-9]+) threads", log_message):
            (transport, total_time_ms, case_name, num_blobs,
             blob_size, concurrency) = m.group(1, 2, 3, 4, 5, 6)
            total_time_ms = int(total_time_ms)
            num_blobs = int(num_blobs)
            blob_size = int(blob_size)
            concurrency = int(concurrency)
            for c in suite.cases:
                if transport == c.transport and case_name == c.case_name and blob_size == c.transfer_config.blob_size and num_blobs == c.transfer_config.num_blobs and concurrency == c.transfer_config.concurrency:
                    c.total_time_ms.append(total_time_ms)
                    break
            else:
                assert(False)

    assert(len(suite.environment.OS) != 0)
    assert(len(suite.environment.compiler) != 0)
    assert(len(suite.environment.storage_account) != 0)
    assert(len(suite.environment.azure_core_version) != 0)
    assert(len(suite.environment.azure_storage_common_version) != 0)
    assert(len(suite.environment.azure_storage_blobs_version) != 0)
    assert(len(suite.baseline_transport) != 0)
    for c in suite.cases:
        assert(len(c.total_time_ms) == repeat_times)
    return suite


suite = parse_log_file(
    "/Users/jamis/Downloads/2022-04-10T09:49:10Z-d2206e0.log")


a = airium.Airium()
a('<!DOCTYPE html>')
with a.html(lang="en"):
    with a.head():
        a.meta(charset="utf-8")
        a.title(_t="Benchmarking Reports")
        a.link(href="styles/table.css", rel="stylesheet", type="text/css")
    with a.body():
        with a.table():
            with a.tr():
                a.th(_t="")
                a.th(_t="blob size")
                a.th(_t="number of blobs")
                a.th(_t="concurrency")
                a.th(_t="baseline({})".format(suite.baseline_transport))
                for t in suite.transports[1:]:
                    a.th(_t=t)
                    a.th(_t="% of baseline")

            last_case_name = None
            for (c, tc) in itertools.product(suite.benchmark_cases, suite.transfer_configs):
                with a.tr():
                    if c != last_case_name:
                        a.td(_t=c, rowspan=len(suite.transfer_configs))
                        last_case_name = c
                    a.td(_t=size_format(tc.blob_size))
                    a.td(_t=tc.num_blobs)
                    a.td(_t=tc.concurrency)

                    tc_total_size = tc.blob_size * tc.num_blobs

                    filtered_results = list(filter(lambda r: r.case_name == c and
                                                   r.transfer_config.blob_size == tc.blob_size and
                                                   r.transfer_config.num_blobs == tc.num_blobs and
                                                   r.transfer_config.concurrency, suite.cases))
                    baseline_time = list(filter(
                        lambda r: r.transport == suite.baseline_transport, filtered_results))[0].total_time_ms
                    baseline_avg_time = numpy.mean(sorted(baseline_time)[1:-1])
                    baseline_cv = numpy.std(
                        baseline_time) / numpy.mean(baseline_time)
                    baseline_speed = tc_total_size / baseline_avg_time * 1000

                    td_title = ", ".join(
                        [str(i) + "ms" for i in baseline_time]) + "; " + f"{baseline_cv:.3f}"
                    td_data = size_format(baseline_speed) + "/s"
                    if baseline_cv > 0.3:
                        td_data += "*"
                    a.td(_t=td_data, title=td_title)

                    for r in filter(lambda r: r.transport != suite.baseline_transport, filtered_results):
                        avg_time = numpy.mean(sorted(r.total_time_ms)[1:-1])
                        cv = numpy.std(r.total_time_ms) / \
                            numpy.mean(r.total_time_ms)
                        speed = tc_total_size / avg_time * 1000
                        percent = speed/baseline_speed * 100

                        td_title = ", ".join(
                            [str(i) + "ms" for i in r.total_time_ms]) + "; " + f"{cv:.3f}"
                        td_data = size_format(speed) + "/s"
                        if cv > 0.3:
                            td_data += "*"
                        a.td(_t=td_data, title=td_title)
                        a.td(_t=f"{percent:.1f}" + "%")
        with a.ul():
            a.li(_t="OS: {}".format(suite.environment.OS))
            a.li(_t="compiler: {}".format(suite.environment.compiler))
            with a.li(_t="library version:").ul():
                a.li(_t=suite.environment.azure_core_version)
                a.li(_t=suite.environment.azure_storage_common_version)
                a.li(_t=suite.environment.azure_storage_blobs_version)


with open("reports.html", "wb") as f:
    f.write(bytes(a))
