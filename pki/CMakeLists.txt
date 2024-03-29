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

if(UNIX OR CYGWIN)
  configure_file(icinga2-build-ca.cmake ${CMAKE_CURRENT_BINARY_DIR}/icinga2-build-ca @ONLY)
  configure_file(icinga2-build-key.cmake ${CMAKE_CURRENT_BINARY_DIR}/icinga2-build-key @ONLY)
  configure_file(icinga2-sign-key.cmake ${CMAKE_CURRENT_BINARY_DIR}/icinga2-sign-key @ONLY)
  configure_file(icinga2-setup-agent.cmake ${CMAKE_CURRENT_BINARY_DIR}/icinga2-setup-agent @ONLY)

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/icinga2-build-ca ${CMAKE_CURRENT_BINARY_DIR}/icinga2-build-key
          ${CMAKE_CURRENT_BINARY_DIR}/icinga2-sign-key
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/icinga2-setup-agent
    DESTINATION ${CMAKE_INSTALL_SBINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )

  install(
    FILES openssl.cnf openssl-quiet.cnf pkifuncs vars
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icinga2/pki
  )
endif()
