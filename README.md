# Airborne Parrot Firmware Research

> [!WARNING]
> For educational and research purposes only. Follow all local laws.

<img src="https://www.kindpng.com/picc/m/713-7130959_parrot-mini-drone-airborne-night-swat-transp-hd.png" alt="Parrot Mini Drone Airborne Night Swat Transp, HD Png Download@kindpng.com">

## Overview

This document collects my finding about the Parrot **Airborne Night (SWAT)** firmware with the goal of enabling BNEP/IP networking and potentially running [Paparazzi](https://en.wikipedia.org/wiki/Paparazzi_Project) or other custom autopilot stacks.

## Motive

I have a Parrot Airborne Night Drone that my brother gave me about 8 years ago. The company [ditched consumer drones](https://www.digitalcameraworld.com/news/parrot-ditches-consumer-drones-and-its-profits-take-off) in 2021 but it's making huge profits on B2B.

## Capabilities

It's a [quadcopter drone](https://en.wikipedia.org/wiki/Quadcopter) with:

- 480p snapshot camera
- Two right, built-in LED lights
- Speeds up to 11 mph
- 1 GB internal flash memory
- Performs 360 forward and backward flips
- Circuit breaker cuts engines immediately in even of collision
- 550 mAh battery
- 7"W x 1-3/8"H x 7-1/4"D
- Weight: 1.9 oz.

You can learn more about the Parrot minidrone from the [user guide](https://www.parrot.com/assets/s3fs-public/2021-09/airborne-night-drone_user-guide_uk.pdf).

## Offical firmware

You can find the offical firmware for the Parrot Airborne on the [Parrot site](https://www.parrot.com/en/support/documentation/airborne).

1. **Kernel:** `2.6.36-102952-g6aedf2e`
2. **BusyBox:** `v1.20.2 (2017-03-22)`

## What's missing?

You're limited to remote control commands over BLE using the FreeFlight app.

The Airborne Night can't create a IP network over bluetooth (no [BNEP](https://www.bluetooth.com/specifications/specs/bluetooth-network-encapsulation-protocol-1-0/) interface). Without it you can't stream data, custom on-board code, telemetry, etc. The wiki article on [Paparazziuav](https://wiki.paparazziuav.org/wiki/Ap.parrot_minidrone#Supported_Minidrones) suggests:

> One could try to hack a Mambo firmware on the Airborne Night types somehow.

I want to be able to create a library similar to [minidrone found in Go](https://pkg.go.dev/github.com/eric-erki/gobot/platforms/parrot/minidrone#section-readme) but supporting a custom firmware. Unfortunately, there isn't any SDK you can install to make this easier, such as [MathWorks Parrot Minidrone Support](https://www.mathworks.com/hardware-support/parrot-minidrones.html).
