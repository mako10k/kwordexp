#!/bin/bash

autoreconf -ivf
./configure
make
sudo make install