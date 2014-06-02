# <a id="about-icinga2"></a> About Icinga 2

## <a id="what-is-icinga2"></a> What is Icinga 2?

Icinga 2 is an open source monitoring system which checks the availability of your
network resources, notifies users of outages, and generates performance data for reporting.

Scalable and extensible, Icinga 2 can monitor large, complex environments across
multiple locations.

## <a id="licensing"></a> Licensing

Icinga 2 and the Icinga 2 documentation are licensed under the terms of the GNU
General Public License Version 2, you will find a copy of this license in the
LICENSE file included in the source package.

## <a id="support"></a> Support

Support for Icinga 2 is available in a number of ways. Please have a look at
the support overview page at [https://support.icinga.org].

## <a id="contribute"></a> Contribute

There are many ways to contribute to Icinga - whether it be sending patches, testing,
reporting bugs, or reviewing and updating the documentation. Every contribution
is appreciated!

Please get in touch with the Icinga team at [https://www.icinga.org/ecosystem/].

## <a id="development"></a> Icinga 2 Development

You can follow Icinga 2's development closely by checking
out these resources:

* Development Bug Tracker: [https://dev.icinga.org/projects/i2?jump=issues] ([http://www.icinga.org/faq/how-to-report-a-bug/])
* Git Repositories: [https://git.icinga.org/?p=icinga2.git;a=summary] (mirror at [https://github.com/Icinga/icinga2])
* Git Checkins Mailinglist: [https://lists.icinga.org/mailman/listinfo/icinga-checkins]
* Development Mailinglist: [https://lists.icinga.org/mailman/listinfo/icinga-devel]
* \#icinga-devel on irc.freenode.net [http://webchat.freenode.net/?channels=icinga-devel] including a Git Commit Bot

For general support questions, please refer to [https://www.icinga.org/support/].

## <a id="demo-vm"></a> Demo VM

Icinga 2 is available as [Vagrant Demo VM](#vagrant).

## <a id="whats-new"></a> What's new

<<<<<<< HEAD
### What's New in Version 2.0.0 Beta 1
=======
### What's New in Version 0.0.10

* Host and Service are now checkable. #5919
* Support new lines in addition to commas to separate object attributes. #5901
* Add group membership assign rules. #5910
* Support nested groups. #5858
* Add apply target type. #5924
* Add relative object names. #5925
* Merge macros and custom into 'vars' dictionary. Changed runtime macros and environment variable export. #5855
* Add support for modified attributes for custom attributes. #5956
* Allow to assign var values to existing vars evaluted on runtime. #5959
* Rename/shorten attribute names and filter variables. #5857 
* Remove the 'Icinga' prefix for global constants. #5960
* Global option to enable/disable host/service checks. #5975
* Add legacy attributes to host, service and group objects: `address{,6}'`, `notes`, `notes_url`, `action_url`, `icon_image`, `icon_image_alt`. #5856
* Support "#" hash comments. #5994
* Cluster: Spanning Tree like communication. #5467
* Properly implement the Process class for Windows. #3684
>>>>>>> Fixes for poor grammar and bad sentence structure.

Lots of things. Please read [Icinga 2 in a nutshell](#icinga2-in-a-nutshell).

#### Changes

<<<<<<< HEAD
* Updated sample configuration for final release.
=======
* For a detailed list of changes check out [#5909](https://dev.icinga.org/issues/5909)
* DB IDO schema upgrade required.
>>>>>>> Fixes for poor grammar and bad sentence structure.

### Archive

Please check the `ChangeLog` file.

## <a id="icinga2-in-a-nutshell"></a> Icinga 2 in a Nutshell

* Use [Packages](#getting-started)

Look for available packages on [http://packages.icinga.org] or ask your distribution's maintainer.
Compiling from source is not recommended, and not the default either.

* Real Distributed Architecture

[Cluster](#distributed-monitoring-high-availability) model for distributed setups, load balancing
and High-Availability installations. Secured by SSL x509 certificates, supporting IPv4 and IPv6.

* High Performance

Multithreaded and scalable for small embedded systems as well as large scale environments.
Running checks every second - not an issue anymore.

* Modular & flexible [features](#features)

Enable only the features required for the local installation. Using Icinga Web 2 and requiring
DB IDO, but no status data? No problem, just enable ido-mysql and disable statusdata.
Another example: Graphite should be enabled on a dedicated cluster node. Enable it over there
and point it to the carbon cache socket.

* Native support for the [Livestatus protocol](#setting-up-livestatus)

Next to the Icinga 1.x status and log files and the IDO database backend the commonly used
Livestatus protocol is available with Icinga 2. Either as unix or tcp socket.

* Native support for [Graphite](#graphite-carbon-cache-writer)

Icinga 2 still supports writing performance data files for graphing addons, but also adds the
capability of writing performance data directly onto a Graphite tcp socket simplifying realtime
monitoring graphs.

* Dynamic configuration language

Simple [apply](#using-apply) and [assign](#group-assign) rules for creating configuration object
relationships based on patterns. Supported with [duration literals](#duration-literals) for interval
attributes, [expression operators](#expression-operators), [function calls](#function-calls) for
pattern and regex matching and (global) [constants](#constants).
Sample configuration for common plugins is shipped with Icinga 2 as
part of the [Icinga Template Library](#itl).

* Revamped Commands

One command to rule them all - supporting optional and conditional [command arguments](#commands-arguments).
[Environment variables](#command-environment-variables) exported on-demand populated with
runtime evaluated macros.
Three types of commands used for different actions: checks, notifications and events.
Check timeout for commands instead of a global option. Command custom attributes allowing
you to specify default values for the command.

* Custom Runtime Macros

Access [custom attributes](#custom-attributes) with their short name, for example $mysql_user$,
or any object attribute, for example $host.notes$. Additional macros with runtime and statistic
information are available as well. Use these [runtime macros](#runtime-custom-attributes) in
the command line, environment variables and custom attribute assignments.

* Notifications simplified

Multiple [notifications](#notifications) for one host or service with existing users
and notification commands. No more duplicated contacts for different notification types.
Telling notification filters by state and type, even more fine-grained than Icinga 1.x.
[Escalation notifications](#notification-escalations) and [delayed notifications](#first-notification-delay)
are just notifications with additional begin and/or end time attribute.

* Dependencies between Hosts and Services

Classic [dependencies](#dependencies) between host and parent hosts, and services and parent services work the
same way as "mixed" dependencies from a service to a parent host and vice versa. Host checks
depending on an upstream link port (as service) are not a problem anymore.
No more additional parents settings - host dependencies already define the host parent relationship
required for network reachability calculations.

* [Recurring Downtimes](#recurring-downtimes)

Forget the external cronjob scheduling downtimes on-demand. Configure them right away as Icinga 2
configuration objects and set their active time window.

* Embedded Health Checks

No more external statistic tool but an [instance](#itl-icinga) and [cluster](#itl-cluster) health
check providing direct statistics as performance data for your graphing addon, for example Graphite.

* Compatibility with Icinga 1.x

All known interfaces are optional available: [status files](#status-data), [logs](#compat-logging),
[DB IDO](#configuring-ido) MySQL/PostgreSQL, [performance data](#performance-data),
[external command pipe](#external-commands) and for migration reasons a
[checkresult file reader](#check-result-files) too.
All [Monitoring Plugins](#setting-up-check-plugins) can be integrated into Icinga 2 with
newly created check command configuration if not already provided.
[Configuration migration](#configuration-migration) is possible through the Icinga Web 2 CLI tool.
Additional information on the differences is documented in the [migration](#differences-1x-2) chapter.

* [Vagrant Demo VM](#vagrant)

Used for demo cases and development tests. Get Icinga 2 running within minutes and spread the #monitoringlove
to your friends and partners.



