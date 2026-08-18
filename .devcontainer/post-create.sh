#!/bin/bash
set -e

west init -l application
west update
west zephyr-export
west blobs fetch hal_espressif

sudo /opt/python/venv/bin/pip install -r /workdir/zephyr/scripts/requirements.txt
