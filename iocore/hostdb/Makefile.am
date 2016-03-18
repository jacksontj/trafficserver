# Makefile.am for the traffic/iocore/hostdb hierarchy
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils

EXTRA_DIST = I_HostDB.h

TESTS = $(check_PROGRAMS)

noinst_LIBRARIES = libinkhostdb.a

libinkhostdb_a_SOURCES = \
  HostDB.cc \
  I_HostDB.h \
  I_HostDBProcessor.h \
  Inline.cc \
  MultiCache.cc \
  P_HostDB.h \
  P_HostDBProcessor.h \
  P_MultiCache.h 
  
check_PROGRAMS = test_foo

test_LD_FLAGS = \
  @EXTRA_CXX_LDFLAGS@ \
  @LIBTOOL_LINK_FLAGS@ \
  @OPENSSL_LDFLAGS@

test_CPP_FLAGS = \
  $(AM_CPPFLAGS) \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy/api/ts \
  -I$(top_srcdir)/proxy/api \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/lib/ts \
  @OPENSSL_INCLUDES@

test_LD_ADD = \
  libinkhostdb.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/iocore/hostdb/libinkhostdb.a \
  $(top_builddir)/iocore/dns/libinkdns.a \
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  #$(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ @HWLOC_LIBS@
  
test_foo_SOURCES = \
	test_foo.cc
test_foo_CPPFLAGS = $(test_CPP_FLAGS)
test_foo_LDFLAGS = $(test_LD_FLAGS)
test_foo_LDADD = $(test_LD_ADD)

#test_UNUSED_SOURCES = \
#  test_I_HostDB.cc \
#  test_P_HostDB.cc
