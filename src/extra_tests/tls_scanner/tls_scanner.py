#!/usr/bin/python2

import sys
import subprocess
import re

def format_report(client_output):
    version_re = re.compile('TLS (v1\.[0-2]) using ([A-Z0-9_]+)')

    version_match = version_re.search(client_output)

    #print client_output

    if version_match:
        return "Established %s %s" % (version_match.group(1), version_match.group(2))
    else:
        return client_output

def scanner(args = None):
    if args is None:
        args = sys.argv

    if len(args) != 2:
        print "Error: Usage tls_scanner.py host_file"
        return 2

    scanners = {}

    for url in [s.strip() for s in open(args[1]).readlines()]:
        scanners[url] = subprocess.Popen(['../../../botan', 'tls_client', url], stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.PIPE)

    for url in scanners.keys():
        scanners[url].stdin.close()

    report = {}

    for url in scanners.keys():
        print "waiting for", url
        scanners[url].wait()

        if scanners[url].returncode != None:
            output = scanners[url].stdout.read() + scanners[url].stderr.read()
            report[url] = format_report(output)

    for url in report.keys():
        print url, ":", report[url]

    return 0

if __name__ == '__main__':
    sys.exit(scanner())