#!/usr/bin/env python

"""Ninja build configurator for network library"""

import sys
import os

sys.path.insert( 0, os.path.join( 'build', 'ninja' ) )

import generator

dependlibs = [ 'foundation' ]

generator = generator.Generator( project = 'network', dependlibs = dependlibs, variables = [ ( 'bundleidentifier', 'com.rampantpixels.network.$(binname)' ) ] )
target = generator.target
writer = generator.writer
toolchain = generator.toolchain

network_lib = generator.lib( module = 'network', sources = [
  'address.c', 'network.c', 'poll.c', 'socket.c', 'tcp.c', 'udp.c', 'version.c' ] )

includepaths = generator.test_includepaths()

extralibs = []
if target.is_windows():
  extralibs += [ 'iphlpapi', 'ws2_32' ]

if not target.is_ios() and not target.is_android():
  configs = [ config for config in toolchain.configs if config not in [ 'profile', 'deploy' ] ]
  if not configs == []:
    generator.bin( 'blast', [ 'main.c', 'client.c', 'reader.c', 'server.c', 'writer.c' ], 'blast', basepath = 'tools', implicit_deps = [ network_lib ], libs = [ 'network', 'foundation' ] + extralibs, configs = configs )

test_cases = [
  'address', 'socket', 'tcp', 'udp'
]
if target.is_ios() or target.is_android() or target.is_pnacl():
  #Build one fat binary with all test cases
  test_resources = []
  test_extrasources = []
  test_cases += [ 'all' ]
  if target.is_ios():
    test_resources = [ os.path.join( 'all', 'ios', item ) for item in [ 'test-all.plist', 'Images.xcassets', 'test-all.xib' ] ]
    test_extrasources = [ os.path.join( 'all', 'ios', 'viewcontroller.m' ) ]
  elif target.is_android():
    test_resources = [ os.path.join( 'all', 'android', item ) for item in [
      'AndroidManifest.xml', os.path.join( 'layout', 'main.xml' ), os.path.join( 'values', 'strings.xml' ),
      os.path.join( 'drawable-ldpi', 'icon.png' ), os.path.join( 'drawable-mdpi', 'icon.png' ), os.path.join( 'drawable-hdpi', 'icon.png' ),
      os.path.join( 'drawable-xhdpi', 'icon.png' ), os.path.join( 'drawable-xxhdpi', 'icon.png' ), os.path.join( 'drawable-xxxhdpi', 'icon.png' )
    ] ]
    test_extrasources = [ os.path.join( 'all', 'android', 'java', 'com', 'rampantpixels', 'foundation', 'test', item ) for item in [
      'TestActivity.java'
    ] ]
  if target.is_pnacl():
    generator.bin( module = '', sources = [ os.path.join( module, 'main.c' ) for module in test_cases ] + test_extrasources, binname = 'test-all', basepath = 'test', implicit_deps = [ network_lib ], libs = [ 'network', 'test', 'foundation' ], resources = test_resources, includepaths = includepaths )
  else:
    generator.app( module = '', sources = [ os.path.join( module, 'main.c' ) for module in test_cases ] + test_extrasources, binname = 'test-all', basepath = 'test', implicit_deps = [ network_lib ], libs = [ 'network', 'test', 'foundation' ], resources = test_resources, includepaths = includepaths )
else:
  #Build one binary per test case
  generator.bin( module = 'all', sources = [ 'main.c' ], binname = 'test-all', basepath = 'test', implicit_deps = [ network_lib ], libs = [ 'network', 'foundation' ] + extralibs, includepaths = includepaths )
  for test in test_cases:
    generator.bin( module = test, sources = [ 'main.c' ], binname = 'test-' + test, basepath = 'test', implicit_deps = [ network_lib ], libs = [ 'test', 'network', 'foundation' ] + extralibs, includepaths = includepaths )
