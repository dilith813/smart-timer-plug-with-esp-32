# smart-timer-plug-with-esp-32
A hybrid smart timer plug that users can set the time through their phone, or via a dial on the plug.

## Introduction

This was part of a project my group did for the semester 2 module EN1190 - Electronic Design Project of the Department of Electronic and Telecommunication Engineering, University of Moratuwa. My contribution towards this project was upgrading the initial skeleton code we had for the functionality of the ESP board, and building an android app for remote control.

## Project Overview

The smart plug was created as a solution for the busy and inattentive nature of people while using electical appliances. Our product provides the ability to control how long the plug should be running for, to minimize unwanted electricity usage and prevent hazards. Since many such devices exist on the market already, our main focus was on making this product more accessible by including both online and offline options. 

With the way our product is designed, users can set the time manually by rotating a dial. They will also have the capabilities to pause, continue and reset the time through the same dial. All these options, and the timer countdown is displayed on a small OLED screen.

Regarding the online mode, we implemented the same capabilities of the dial (setting the time, resetting, pausing) to be controlled through a phone app. This information would then be synced between the plug and the phone app, through the backend services of Google Firebase.

Initially, the user will have the choice of using the plug in online mode, or offline mode. But if internet connection to the plug is disrupted while it's running, it will switch automatically to offline mode. There's a few other scenarios we had come across, which could cause disruptions to the functionality, but due to the time constraints, there simply wasn't enough room to provide solutions and workarounds for all of them.
