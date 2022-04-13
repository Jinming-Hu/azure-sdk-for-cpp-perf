#!/usr/bin/env python3

import os
import re
import sys
import numpy
import pathlib
import logging
import datetime
import itertools
import contextlib
import urllib.parse
from dataclasses import dataclass, field
import concurrent.futures
import airium
from github import Github
import azure.identity
import azure.mgmt.storage
import azure.mgmt.compute
import azure.mgmt.network
import azure.storage.blob


def size_format(num):
    for unit in ["", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi"]:
        if abs(num) < 1024.0:
            return f"{num:g} {unit}B"
        num /= 1024.0
    return f"{num:g} YiB"


def get_storage_account_info(account_name):
    if account_name in get_storage_account_info.cache:
        return get_storage_account_info.cache[account_name]

    account_desc = "unknown"
    if "AZURE_SUBSCRIPTION_ID" not in os.environ:
        logging.warning(
            "AZURE_SUBSCRIPTION_ID environment variable not defined")
    else:
        subscription_id = os.environ["AZURE_SUBSCRIPTION_ID"]
        credential = azure.identity.AzureCliCredential()
        storage_client = azure.mgmt.storage.StorageManagementClient(
            credential, subscription_id)
        try:
            for a in storage_client.storage_accounts.list():
                if a.name == account_name:
                    account_desc = f"{a.sku.name}, {a.kind}, {a.location}"
                    break
            else:
                logging.warning(
                    f"cannot find storage account {account_name} under subscription {subscription_id}")
        except azure.identity.CredentialUnavailableError as e:
            logging.warning(e)
    get_storage_account_info.cache[account_name] = account_desc
    return account_desc


get_storage_account_info.cache = {}


def get_storage_account_key(account_name):
    if "AZURE_SUBSCRIPTION_ID" not in os.environ:
        logging.warning(
            "AZURE_SUBSCRIPTION_ID environment variable not defined")
        return
    subscription_id = os.environ["AZURE_SUBSCRIPTION_ID"]
    credential = azure.identity.AzureCliCredential()
    storage_client = azure.mgmt.storage.StorageManagementClient(
        credential, subscription_id)
    try:
        for a in storage_client.storage_accounts.list():
            if a.name == account_name:
                storage_account_id_pattern = "/subscriptions/.+/resourceGroups/(.+)/providers/Microsoft.Storage/storageAccounts/.+"
                m = re.fullmatch(storage_account_id_pattern, a.id)
                resource_group = m.group(1)
                break
        account_keys = storage_client.storage_accounts.list_keys(
            resource_group, account_name)
        return account_keys.keys[0].value
    except azure.identity.CredentialUnavailableError as e:
        logging.warning(e)


def get_azure_vm_info(vm_id):
    if vm_id in get_azure_vm_info.cache:
        return get_azure_vm_info.cache[vm_id]

    vm_desc = "unknown"
    vm_id_pattern = "/subscriptions/(.+)/resourceGroups/(.+)/providers/Microsoft.Compute/virtualMachines/(.+)"
    nic_id_pattern = "/subscriptions/(.+)/resourceGroups/(.+)/providers/Microsoft.Network/networkInterfaces/(.+)"
    if m := re.fullmatch(vm_id_pattern, vm_id):
        (subscription_id, resource_group, vm_name) = m.group(1, 2, 3)

        credential = azure.identity.AzureCliCredential()
        compute_client = azure.mgmt.compute.ComputeManagementClient(
            credential, subscription_id)
        try:
            vm_info = compute_client.virtual_machines.get(
                resource_group, vm_name)
        except azure.identity.CredentialUnavailableError as e:
            logging.warning(e)
            vm_info = None

        nic_desc = "status unknown"
        if vm_info and len(vm_info.network_profile.network_interfaces) == 1 and (m := re.fullmatch(nic_id_pattern, vm_info.network_profile.network_interfaces[0].id)):
            (subscription_id, resource_group, nic_name) = m.group(1, 2, 3)
            network_client = azure.mgmt.network.NetworkManagementClient(
                credential, subscription_id)
            try:
                network_info = network_client.network_interfaces.get(
                    resource_group, nic_name)
                nic_desc = "enabled" if network_info.enable_accelerated_networking else "not enabled"
            except azure.identity.CredentialUnavailableError as e:
                logging.warning(e)

        vm_desc = f"{vm_info.hardware_profile.vm_size}, {vm_info.location}, accelerated networking {nic_desc}"
    else:
        logging.warning(f"cannot parse vm {vm_id}")

    get_azure_vm_info.cache[vm_id] = vm_desc
    return vm_desc


get_azure_vm_info.cache = {}


def get_name_for_report(suites):
    if not(all(suites[i].environment.azure_core_version == suites[0].environment.azure_core_version for i in range(len(suites))) and all(suites[i].environment.azure_storage_common_version == suites[0].environment.azure_storage_common_version for i in range(len(suites))) and all(suites[i].environment.azure_storage_blobs_version == suites[0].environment.azure_storage_blobs_version for i in range(len(suites)))):
        logs_filename = [os.path.basename(urllib.parse.unquote(
            urllib.parse.urlparse(s.log_source).path)) for s in suites]
        logs_hash = [re.fullmatch(".+-([0-9a-z]+).log", f).group(0)
                     for f in logs_filename]
        return "-".join(logs_hash)

    @dataclass
    class package_version:
        name: str
        major: int
        minor: int
        patch: int
        beta: int

        def __eq__(self, other):
            assert(self.name == other.name)
            return self.major == other.major and self.minor == other.minor and self.patch == other.patch and self.beta == other.beta

        def __lt__(self, other):
            assert(self.name == other.name)
            return (self.major, self.minor, self.patch, self.beta if self.beta != 0 else sys.maxsize) < (other.major, other.minor, other.patch, other.beta if other.beta != 0 else sys.maxsize)

        def __repr__(self):
            s = f"{self.name}_{self.major}.{self.minor}.{self.patch}"
            if self.beta == 0:
                return s
            else:
                return f"{s}-beta.{self.beta}"

    def parse_package_version(name):
        m = re.fullmatch("(.+)_(\d+).(\d+).(\d+)(-beta.(\d+))?", name)
        v = package_version(m.group(1), int(m.group(2)),
                            int(m.group(3)), int(m.group(4)), 0)
        if m.group(6):
            v.beta = int(m.group(6))
        return v

    if not get_name_for_report.cached:
        azcppsdk_repo = "Azure/azure-sdk-for-cpp"
        g = Github()
        repo = g.get_repo(azcppsdk_repo)
        get_name_for_report.azure_core_versions = []
        get_name_for_report.azure_storage_common_versions = []
        get_name_for_report.azure_storage_blobs_versions = []
        for t in repo.get_releases():
            v = parse_package_version(t.tag_name)
            d = t.published_at
            d = d + datetime.timedelta(days=3)
            d = d.replace(day=1, hour=0, minute=0, second=0, microsecond=0)
            if v.name == "azure-core":
                get_name_for_report.azure_core_versions.append((v, d))
            elif v.name == "azure-storage-common":
                get_name_for_report.azure_storage_common_versions.append(
                    (v, d))
            elif v.name == "azure-storage-blobs":
                get_name_for_report.azure_storage_blobs_versions.append((v, d))
        get_name_for_report.azure_core_versions.sort()
        l = get_name_for_report.azure_core_versions
        assert(all(l[i][1] <= l[i+1][1] for i in range(len(l) - 1)))
        get_name_for_report.azure_storage_common_versions.sort()
        l = get_name_for_report.azure_storage_common_versions
        assert(all(l[i][1] <= l[i+1][1] for i in range(len(l) - 1)))
        get_name_for_report.azure_storage_blobs_versions.sort()
        l = get_name_for_report.azure_storage_blobs_versions
        assert(all(l[i][1] <= l[i+1][1] for i in range(len(l) - 1)))
        get_name_for_report.cached = True

    v1 = parse_package_version(suites[0].environment.azure_core_version)
    v2 = parse_package_version(
        suites[0].environment.azure_storage_common_version)
    v3 = parse_package_version(
        suites[0].environment.azure_storage_blobs_version)

    v1l = get_name_for_report.azure_core_versions
    v2l = get_name_for_report.azure_storage_common_versions
    v3l = get_name_for_report.azure_storage_blobs_versions
    consider_beta = any(v.beta != 0 for v in [v1, v2, v3])
    if not consider_beta:
        v1l = list(filter(lambda i: i[0].beta == 0, v1l))
        v2l = list(filter(lambda i: i[0].beta == 0, v2l))
        v3l = list(filter(lambda i: i[0].beta == 0, v3l))

    v1i = next(i for i, v in enumerate(v1l) if v[0] == v1)
    v2i = next(i for i, v in enumerate(v2l) if v[0] == v2)
    v3i = next(i for i, v in enumerate(v3l) if v[0] == v3)

    latest_release_date = max([v1l[v1i][1], v2l[v2i][1], v3l[v3i][1]])

    if v1l[v1i][1] <= latest_release_date and (v1i + 1 >= len(v1l) or v1l[v1i+1][1] > latest_release_date) and v2l[v2i][1] <= latest_release_date and (v2i + 1 >= len(v2l) or v2l[v2i+1][1] > latest_release_date) and v3l[v3i][1] <= latest_release_date and (v3i + 1 >= len(v3l) or v3l[v3i+1][1] > latest_release_date):
        return latest_release_date.strftime("%b %Y") + " " + ("Preview" if consider_beta else "GA") + " Release"
    else:
        return f"{str(v1)} {str(v2)} {str(v3)}"


get_name_for_report.cached = False


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
    environment: benchmark_environment = field(
        default_factory=benchmark_environment)
    transports: list[str] = field(default_factory=list)
    baseline_transport: str = ""
    benchmark_cases: list[str] = field(default_factory=list)
    transfer_configs: list[transfer_configuration] = field(
        default_factory=list)
    cases: list[benchmark_case] = field(default_factory=list)
    start_time: datetime.datetime = datetime.datetime.now()
    end_time: datetime.datetime = datetime.datetime.now()
    log_source: str = ""


def parse_log(content):
    suite = benchmark_suite()
    env = benchmark_environment()

    repeat_times = None

    line_pattern = re.compile("^\[(.+)\] \[(.+)\] (.+)$")
    for l in content.splitlines():
        match_object = re.fullmatch(line_pattern, l)
        log_time = datetime.datetime.strptime(
            match_object.group(1), "%Y-%m-%d %H:%M:%S.%f")
        (log_level, log_message) = match_object.group(2, 3)

        if log_message == "started":
            suite.start_time = log_time
        elif log_message == "exited":
            suite.end_time = log_time
        elif m := re.fullmatch("using storage account: (.+)", log_message):
            suite.environment.storage_account = get_storage_account_info(
                m.group(1))
        elif m := re.fullmatch("Azure VM resource ID: (.+)", log_message):
            suite.environment.azure_vm = get_azure_vm_info(m.group(1))
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
        elif m := re.fullmatch("tr?ansfer config (\d+): blob size: (\d+) bytes, number of blobs: (\d+), concurrency: (\d+)", log_message):
            suite.transfer_configs.append(transfer_configuration(
                int(m.group(2)), int(m.group(3)), int(m.group(4))))
            assert(len(suite.transfer_configs) == int(m.group(1)))
        elif m := re.fullmatch("repeat times: (\d+)", log_message):
            repeat_times = int(m.group(1))
            for c in suite.benchmark_cases:
                for tc in suite.transfer_configs:
                    for t in suite.transports:
                        suite.cases.append(benchmark_case(c, tc, t))
        elif m := re.fullmatch("(.+) used (\d+)ms to (.+) (\d+) (\d+)-byte blobs with (\d+) threads", log_message):
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


def generate_suite_report(a, suite):
    with a.table():
        with a.thead().tr():
            a.th(_t="")
            a.th(_t="blob size")
            a.th(_t="number of blobs")
            a.th(_t="concurrency")
            a.th(_t=f"baseline({suite.baseline_transport})")
            for t in suite.transports[1:]:
                a.th(_t=t)
                a.th(_t="% of baseline")

        with a.tbody():
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
                    baseline_avg_time = numpy.mean(
                        sorted(baseline_time)[1:-1])
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
                        avg_time = numpy.mean(
                            sorted(r.total_time_ms)[1:-1])
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
        a.li(
            _t=f"Azure Storage account: {suite.environment.storage_account}")
        a.li(_t=f"Azure VM: {suite.environment.azure_vm}")
        a.li(_t=f"OS: {suite.environment.OS}")
        a.li(_t=f"compiler: {suite.environment.compiler}")
        with a.li(_t="library version:").ul():
            a.li(_t=suite.environment.azure_core_version)
            a.li(_t=suite.environment.azure_storage_common_version)
            a.li(_t=suite.environment.azure_storage_blobs_version)
        a.li(
            _t=f"benchmarking started at {suite.start_time} UTC, ended at {suite.end_time} UTC")
        with a.li(_t="raw log:"):
            logname = os.path.basename(urllib.parse.unquote(
                urllib.parse.urlparse(suite.log_source).path))
            a.a(href=suite.log_source, _t=logname)


@contextlib.contextmanager
def basic_html_body(a, title):
    a("<!DOCTYPE html>")
    with a.html(lang="en"):
        with a.head():
            a.meta(charset="utf-8")
            a.title(_t=title)
            a.link(href="/styles/table.css", rel="stylesheet", type="text/css")
        with a.body():
            yield


def generate_suites_report(suites):
    a = airium.Airium()
    with basic_html_body(a, "Benchmarking Report"):
        for s in suites:
            generate_suite_report(a, s)
            if s != suites[-1]:
                a.br()
                a.hr()
                a.br()
    return bytes(a)


def publish_report(container_client, blob_name, content):
    logging.info(f"publishing report to {blob_name}")
    if "DRY_RUN" in os.environ and os.environ["DRY_RUN"].upper() in ["TRUE", "ON", "1", "YES"]:
        dirname = os.path.dirname(blob_name)
        if dirname:
            pathlib.Path(dirname).mkdir(parents=True, exist_ok=True)
        with open(blob_name, "wb") as f:
            f.write(content)
        return
    html_content_settings = azure.storage.blob.ContentSettings(
        content_type="text/html")
    blob_client = container_client.get_blob_client(blob_name)
    blob_client.upload_blob(
        content, content_settings=html_content_settings, overwrite=True)


if __name__ == "__main__":
    logging.getLogger().setLevel(logging.INFO)
    if len(sys.argv) == 2:
        report_filename = "reports.html"
        suite = parse_log(open(sys.argv[1]).read())
        with open(report_filename, "w") as f:
            f.write(generate_suites_report(suite))
        logging.info(f"saved to {report_filename}")
    elif len(sys.argv) == 1:
        log_account_name = "azsdkcpp"
        raw_log_contianer_name = "raw-log"
        account_key = get_storage_account_key(log_account_name)
        if not account_key:
            exit(1)

        blob_service_client = azure.storage.blob.BlobServiceClient(
            f"https://{log_account_name}.blob.core.windows.net", account_key)
        raw_log_container_client = blob_service_client.get_container_client(
            raw_log_contianer_name)
        if not raw_log_container_client.exists():
            logging.warning(
                f"{raw_log_contianer_name} container doesn't exist in storage account {log_account_name}")
            exit(1)
        log_blobs = filter(lambda b: re.fullmatch(
            "[0-9-]{10}T[0-9:]{8}Z-[0-9a-z]+.log", b), map(lambda b: b.name, raw_log_container_client.list_blobs()))

        def parse_log_from_blob(blob_name):
            blob_client = raw_log_container_client.get_blob_client(blob_name)
            blob_content = blob_client.download_blob().content_as_text()
            suite = parse_log(blob_content)
            suite.log_source = blob_client.url
            return suite

        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
            suites = list(executor.map(
                lambda b: parse_log_from_blob(b), log_blobs))

        def group_projection(suite):
            e = suite.environment
            return f"{e.azure_core_version} {e.azure_storage_common_version} {e.azure_storage_blobs_version}"

        suites_pj = [group_projection(s) for s in suites]
        suite_groups = [list(itertools.compress(
            suites, numpy.array(suites_pj) == i)) for i in set(suites_pj)]
        report_container_client = blob_service_client.get_container_client(
            "$web")

        i = airium.Airium()
        with basic_html_body(i, "Azure Storage C++ SDK Benchmarking Reports"):
            for g in suite_groups:
                report_name = get_name_for_report(g)
                assert(len(report_name) != 0)
                report_filename = "reports/" + \
                    report_name.replace(" ", "_") + ".html"
                report_content = generate_suites_report(g)
                publish_report(report_container_client,
                               report_filename, report_content)
                i.a(_t=report_name, href=report_filename).br()

        publish_report(report_container_client, "index.html", bytes(i))
