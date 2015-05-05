#    Copyright (C) 2004-2009 Fons Adriaensen <fons@kokkinizita.net>
#    
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


CPPFLAGS += -I. -fPIC -D_REENTRANT -Wall -O3


all:	filters.so


filters.so:	filters.o filters_if.o exp2ap.o
	g++ -shared filters.o filters_if.o exp2ap.o -o filters.so

filters.o:	ladspaplugin.h filters.h
filters_if.o:	ladspaplugin.h filters.h

install:	all
	cp  *.so /usr/lib/ladspa

clean:
	/bin/rm -f *~ *.o *.so

