#!/usr/bin/env python3

import fileinput
import parse
import statistics

suites = {}

suite_name = str()
case_name = str()

for line in fileinput.input():
    line.rstrip()

    if len(case_name):
        data = line.strip()
    else:
        if not line.startswith(" "):
            suite_name = line.strip()
        else:
            case_name = line.strip()
        continue

    parse_result = parse.parse("{} ms, {} MiB/s, {} op/s", data)
    if not parse_result:
        raise RuntimeError("failed to parse \"" + data + "\"")
    time_ms = float(parse_result[0])
    mbps = float(parse_result[1])
    opps = float(parse_result[2])
    if suite_name not in suites:
        suites[suite_name] = {}
    if case_name not in suites[suite_name]:
        suites[suite_name][case_name] = {}
        suites[suite_name][case_name]["ms"] = list()
        suites[suite_name][case_name]["mbps"] = list()
        suites[suite_name][case_name]["opps"] = list()
    suites[suite_name][case_name]["ms"].append(time_ms)
    suites[suite_name][case_name]["mbps"].append(mbps)
    suites[suite_name][case_name]["opps"].append(opps)

    case_name = str()

for suite_name in suites:
    print(suite_name)
    for case_name in suites[suite_name]:
        l = suites[suite_name][case_name]
        numres = len(l["ms"])
        print("    " + case_name + "(average of {})".format(numres))
        ms_avg = statistics.mean(l["ms"])
        ms_stdev = statistics.stdev(l["ms"]) / ms_avg
        mbps_avg = statistics.mean(l["mbps"])
        mbps_stdev = statistics.stdev(l["mbps"]) / mbps_avg
        opps_avg = statistics.mean(l["opps"])
        opps_stdev = statistics.stdev(l["opps"]) / opps_avg

        ms_avg = float("%.4g" % ms_avg)
        ms_stdev = float("%.4g" % ms_stdev)
        mbps_avg = float("%.4g" % mbps_avg)
        mbps_stdev = float("%.4g" % mbps_stdev)
        opps_avg = float("%.4g" % opps_avg)
        opps_stdev = float("%.4g" % opps_stdev)

        print("    " + "{} ms ({}), {} MiB/s ({}), {} op/s ({})".format(ms_avg, ms_stdev, mbps_avg, mbps_stdev, opps_avg, opps_stdev))

