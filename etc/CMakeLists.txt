# Icinga 2
# Copyright (C) 2012-2013 Icinga Development Team (http://www.icinga.org/)
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

include(InstallConfig)

install_if_not_exists(icinga2/icinga2.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2)
install_if_not_exists(icinga2/conf.d/generic-host.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/conf.d/generic-service.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/conf.d/groups.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/conf.d/localhost.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/conf.d/macros.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/conf.d/notifications.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/conf.d/timeperiods.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/conf.d/users.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/conf.d)
install_if_not_exists(icinga2/features-available/checker.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/command.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/compatlog.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/debuglog.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/graphite.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/ido-mysql.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/ido-pgsql.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/livestatus.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/notification.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/perfdata.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/statusdata.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)
install_if_not_exists(icinga2/features-available/syslog.conf ${CMAKE_INSTALL_SYSCONFDIR}/icinga2/features-available)

install(CODE "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/icinga2/features-enabled\")")
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink ../features-available/checker.conf \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/icinga2/features-enabled/checker.conf\")")
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink ../features-available/notification.conf \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/icinga2/features-enabled/notification.conf\")")
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink ../features-available/syslog.conf \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/icinga2/features-enabled/syslog.conf\")")

if(NOT WIN32)
  configure_file(init.d/icinga2.cmake ${CMAKE_CURRENT_BINARY_DIR}/init.d/icinga2)
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/init.d/icinga2
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/init.d
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )
endif()