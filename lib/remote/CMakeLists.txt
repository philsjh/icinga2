# Icinga 2
# Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

mkclass_target(endpoint.ti endpoint.th)

mkembedconfig_target(remote-type.conf remote-type.cpp)

add_library(remote SHARED
  endpoint.cpp endpoint.th jsonrpc.cpp remote-type.cpp
)

include_directories(${Boost_INCLUDE_DIRS})
target_link_libraries(remote ${Boost_LIBRARIES} base config)

set_target_properties (
  remote PROPERTIES
  INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}/icinga2
  DEFINE_SYMBOL I2_REMOTE_BUILD
  FOLDER Lib
)

install(
  TARGETS remote
  RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/icinga2
)


