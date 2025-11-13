# Research

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

## What's missing?

You're limited to remote control commands over BLE using the FreeFlight app.

The Airborne Night can't create a IP network over bluetooth (no [BNEP](https://www.bluetooth.com/specifications/specs/bluetooth-network-encapsulation-protocol-1-0/) interface). Without it you can't stream data, custom on-board code, telemetry, etc. The wiki article on [Paparazziuav](https://wiki.paparazziuav.org/wiki/Ap.parrot_minidrone#Supported_Minidrones) suggests:

> One could try to hack a Mambo firmware on the Airborne Night types somehow.

I want to be able to create a library similar to [minidrone found in Go](https://pkg.go.dev/github.com/eric-erki/gobot/platforms/parrot/minidrone#section-readme) but supporting a custom firmware. Unfortunately, there isn't any SDK you can install to make this easier, such as [MathWorks Parrot Minidrone Support](https://www.mathworks.com/hardware-support/parrot-minidrones.html).

## Experiment

[Official Parrot products page](https://www.parrot.com/en/support/documentation).

I need to analyze them side-by-side.

> [!NOTE]
> I'll be using the [MamboEDU firmware](https://github.com/Parrot-Developers/MinidronesEdu/tree/master/mambo-2.6.11) since Paparazzi was tested using that firmware because the original firmware doesn't allow bluetooth network connection.

TBD: Looks to be incremental firmware updates. I think the actual firmware lives on the drone.
