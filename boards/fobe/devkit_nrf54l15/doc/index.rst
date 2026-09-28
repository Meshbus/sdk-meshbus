.. zephyr:board:: devkit_nrf54l15

Configuration sources
*********************

Meshbus supports multiple board targets. Hardware definitions, pin assignments,
console selection and memory layout belong to the adjacent ``board.yml``,
devicetree, pinctrl and Kconfig files. Application overlays may override those
defaults; inspect the selected build's final devicetree and configuration.

Use the SDK `development guide <../../../../DEVELOPMENT.md>`_ for the common
build workflow. Select a target supported by the consuming application's
``sample.yaml`` or ``testcase.yaml``, or use ``west release matrix`` for product
profiles. Build support alone does not establish hardware qualification.

Supported features and runners
******************************

.. zephyr:board-supported-hw::

.. zephyr:board-supported-runners::
