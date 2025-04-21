##
## SpanDSP - a series of DSP components for telephony
##
## Makefile.am - Process this file with automake to produce Makefile.in
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU Lesser General Public License version 2.1,
## as published by the Free Software Foundation.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU Lesser General Public License for more details.
##
## You should have received a copy of the GNU Lesser General Public
## License along with this program; if not, write to the Free Software
## Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

AM_CFLAGS = $(COMP_VENDOR_CFLAGS)
AM_LDFLAGS = $(COMP_VENDOR_LDFLAGS)

noinst_PROGRAMS = fax_tone_detector fax_tone_generator

fax_tone_detector_SOURCES = fax_tone_detector.c
fax_tone_detector_LDADD = -L$(top_builddir)/src -lspandsp -lm

fax_tone_generator_SOURCES = fax_tone_generator.c
fax_tone_generator_LDADD = -L$(top_builddir)/src -lspandsp -lm