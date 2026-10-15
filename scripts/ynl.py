#!/usr/bin/python3
# -*- coding: utf-8 -*-

import os
import re
import sys
SYS_PREFIX_DIR=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(
    os.path.join(
        SYS_PREFIX_DIR,
        'lib/python{:d}.{:d}/dist-packages/'.format(sys.version_info.major,
                                                    sys.version_info.minor)))
from pyynl import cli;
cli.SYS_SCHEMA_DIR=os.path.join(SYS_PREFIX_DIR, 'share/ynl')

if __name__ == '__main__':
    sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])
    sys.exit(cli.main())
