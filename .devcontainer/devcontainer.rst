.. _devcontainer:

Developing with DevContainers
#############################

What is a devcontainer
**********************

To put it simply, a devcontainer is a self-contained development environment, closely coupled to an editor or IDE. It enables developers to not waste time installing and maintaining an up-to-date toolchain for a given project.

It originated from the visual studio code editor, but is now an open specification and has support from other editors `Jetbrains`_ and even a `command-line interface**_.

But, why?
*********



How do I use devcontainers
**************************

This document will detail two ways of using devcontainers. One is to use a
cloud-based instance on github codespaces, the other is using a local
installation of the VSCode editor.

Github codespaces
=================

The `cloud option`_. It is a paid service, but they have `free minutes`_. At the
time of writing, those were 60 hours of uptime per month, using the smallest
machine option (2core, 8GB ram).

1. Log in to github
#. Using a recent web browser, go to `github codespaces`_
#. Click on the "Create codespace" button
#. Select the ``zephyrproject-rtos/zephyr`` repository
#. Select which codespace you want to instantiate
#. Click on "Create codespace"
#. Walk to starbucks, buy a coffee (it takes a while to load)
#. You are dropped into a vscode instance with the Zephyr toolchain. Depending
   on the chosen codespace, you may have a subset of the default west projects
   and some other tools installed (e.g. The Babblesim codespace has a minimal
   west checkout and the babblesim tools installed and updated).

Local VSCode
============

The local option. Completely free. Expect ~10GB of disk usage.

See the `official installation guide`_ for more details.

1. Install docker, add yourself to the `docker` group
1. Install and open `vscode`_
#. Install the `devcontainers extension`_
#. Clone the `zephyr repo`_
#. In VSCode, run the "Dev Containers: `open folder`_ in container" command and
   select the path where the zephyr project is cloned.
#. Select the right devcontainer from the list. E.g. "Bluetooth development with
   Babblesim".
#. Brace for bandwidth (and disk) usage..
#. You are dropped into a vscode instance with the Zephyr toolchain. Depending
   on the chosen codespace, you may have a subset of the default west projects
   and some other tools installed (e.g. The Babblesim codespace has a minimal
   west checkout and the babblesim tools installed and updated).

.. _`Jetbrains`: https://stuff
.. _`command-line interface`: https://stuff
.. _`github codespaces`: https://github.com/codespaces
.. _`free minutes`: https://docs.github.com/en/billing/managing-billing-for-github-codespaces/about-billing-for-github-codespaces#monthly-included-storage-and-core-hours-for-personal-accounts
.. _`cloud option`: https://docs.github.com/en/codespaces/overview
.. _`devcontainers extension`: https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers
.. _`vscode`: https://code.visualstudio.com/
.. _`zephyr repo`: https://github.com/zephyrproject-rtos/zephyr
.. _`official installation guide`: https://code.visualstudio.com/docs/devcontainers/containers#_installation
.. _`open folder`: https://code.visualstudio.com/docs/devcontainers/containers#_quick-start-open-an-existing-folder-in-a-container
