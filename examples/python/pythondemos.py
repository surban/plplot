#!/usr/bin/env python

# Run all python plplot examples non-interactively.

import sys
import os

module_dir = "@MODULE_DIR@"

if module_dir[0] == '@':
	module_dir = os.getcwd ()

sys.path.insert (0, module_dir)

from pl import *

# Parse and process command line arguments
plParseOpts(sys.argv, PL_PARSE_FULL)

# Initialize plplot
plinit()

import xw01
import xw02
import xw03
import xw04
import xw05
import xw06
import xw07
import xw08
import xw09
import xw10
import xw11
import xw12
import xw13
import xw14
import xw15
import xw16
# Comment out xw17.py until can figure a way to bypass this for 
# non-interactive plotting devices where it cannot work.
# import xw17
import xw18
import xw19


# Terminate plplot
plend()
