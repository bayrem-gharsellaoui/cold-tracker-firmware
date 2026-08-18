#!/bin/bash
set -e

sudo /opt/python/venv/bin/python3 -m pip install --upgrade pip

west init -l application
west update
west zephyr-export

sudo /opt/python/venv/bin/pip install -r /workdir/zephyr/scripts/requirements.txt
