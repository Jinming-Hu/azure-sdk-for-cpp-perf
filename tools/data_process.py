#!/usr/bin/env python3

import fileinput
import parse

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
        suites[suite_name][case_name]["numres"] = 0
        suites[suite_name][case_name]["ms"] = 0.0
        suites[suite_name][case_name]["mbps"] = 0.0
        suites[suite_name][case_name]["opps"] = 0.0
    suites[suite_name][case_name]["numres"] += 1
    suites[suite_name][case_name]["ms"] += time_ms
    suites[suite_name][case_name]["mbps"] += mbps
    suites[suite_name][case_name]["opps"] += opps

    case_name = str()

for suite_name in suites:
    print(suite_name)
    for case_name in suites[suite_name]:
        numres = suites[suite_name][case_name]["numres"]
        print("    " + case_name + "(average of {})".format(numres))
        ms = suites[suite_name][case_name]["ms"] / numres
        mbps = suites[suite_name][case_name]["mbps"] / numres
        opps = suites[suite_name][case_name]["opps"] / numres
        print("    " + "{} ms, {} MiB/s, {} op/s".format(ms, mbps, opps))

