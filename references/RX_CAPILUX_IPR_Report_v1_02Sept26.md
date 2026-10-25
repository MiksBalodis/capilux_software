# REXUS IPR, Integration Progress Review

- Flight: REXUS 38
- Experiment: CAPILUX
- Review location: TU Riga
- Date: 02-09-2026

*Figure 1: Team CAPILUX*

## Review board member

- Esmée Menting, SSC Space

## Experiment team members

- Kristofers Nīmanis
- Ugur Golal
- Aleksandrs Kuzevanovs
- Nils Lucāns

---

## Team presentation

Team presentation of project status covering:

- **Mechanical design**
  - LAM moved to below the chambers. Endcap and test chamber redesign: tubes were not precisely cut so they are post-processed (lathed), endcaps modified to have only O-rings over one axis (inner O-rings) and team is considering printing the endcap and then lathing the O-ring groves for improved precision. Lock-wiring of the fasteners will not be done anymore as with the new configuration it is very difficult to reach.
- **Electronics**
  - DSUB and molex connectors updated, new main PCB. Electronics box support will be added to endcaps.
- **Software**
  - Updates to the timeline.
- **Component sourcing**
  - Getting the runcams sponsored was a great improvement.
- **Comments**
  - Upload your IPR presentation to the TeamSite after the review.

---

## Design changes / decisions since CDR / status

### SED, editorial

- Good update to move some of the analyses into the appendix. Core chapters are now more readable.

### Mechanics

- Updated mass estimation: 1.53 kg (excl. bulkhead, within mass budget)
- No mass update since CDR.
- REMARK: it is recommended to have continuous mass measurement of subsystems when they arrive, for easier tracking of deviations from your mass budget. Especially for 3D printed parts, keep a good track of mass of individual (spare) parts, as printer settings heavily influence total mass.
- Module and bulkhead
  - Module + bulkhead currently in transport. Will arrive to Riga soon.
  - DSUB bracket and bulkhead not torqued to spec. Should be done by/at ITW.
- End caps / sealed tank
  - Dimensioning of the O-ring grooves not entirely clear from the drawing (p.164). Insert O-ring groove dimensions in the SED.
  - Information: 3mm O-ring, 41mm inner diameter of groove, 46 outer: 2.5mm "deep". Groove is 4.1mm wide.
  - What would be the effect on your science if the air gets out, but not the liquid? Concern is on the liquid boiling or evaporating. Unsure how much time the air would need to leak out for this to become critical.
  - One open concern that there will be a pressure leak over the cabling of the chamber PCB. This needs to be tested still.
- LAM pads
  - Mounting to the MARA module: team still needs to check with MARA if this is possible/allowed. Mounting to the LAM brackets is an available back-up option.
  - Mounting below the CAPILUX module: OK, just to be checked how to mount it (which mounting holes). Can we sandwich the LAM between the module and bulkhead? Yes, but not particularly recommended as it may be more difficult to properly torque the bulkhead. In this case make sure that you implement some kind of spacers to ensure rigidity when torquing against the LAM.
  - Mounting below the test chambers: how will you ensure correct tightness of the test chamber fastening with a compressible fabric in between? Perhaps a spacer to ensure you can torque against it.
- Coating and submerging
  - 12hrs during test (fully submerged).
  - What are the expectations for a partial submersion? In which orientations of the rocket would the coating be most at risk? Team needs to perform more testing to confirm this.
  - Fluid filling on campaign: it will very realistically be 3-5 days between your last access (flight assembly) and launch.
  - Team notes that the coating is not critical for scientific success.

### Electronics

- Status
  - Main board v2: fully produced, all features have been tested. Recently connected to the RXSM simulator, that also worked apart from uplink.
    - Team would like to still consider implementing overvoltage protection on the camera lines, to protect the cameras.
    - Consider if you want to implement more robustness or redundancy: perhaps split power lines and add a buck boost so that half of the cameras are on 1 power line and the other on a 2nd one.
  - Chamber PCB (pressure + LEDs): multiple v2 PCBs manufactured, 3 integrated, functionality confirmed.
  - TV Mux board (multiplexer): v1 manufactured and tested with 2 cameras, v2 will only feature a connector change and will order it. Lead time 1,5 weeks.
- Power consumption and budget
  - OK, no modifications from CDR.
- Other
  - Cabling: changed for Molex connectors. Good improvement, as well as that you have looked into the connector lifetime (cycles). Recommended to also have spare parts or cables available.
  - Grounding: it is mentioned that the camera shielding (aluminium tape) will be grounded. How will it be connected to ground? Shielding part is not yet completely designed. To-do SSC: check with MORABA if the shielding can be accepted.
  - DSUB orientation changed for improved cabling of the rocket, OK.
  - Datarate: team would like to understand if they potentially could have a higher data rate (/ if they are reaching the limits for their allocation). To-do SSC: check with MORABA what margin there is for the CAPILUX data rate.

### Thermal

- New requirement for vehicle temperature is >5deg C. Note: during roll down (between payload preparation building and launching area) the temperature is not controlled.
- How is the experiment or science affected by freezing? Main risk is the single direction valve, perhaps if water freezes in there it may damage the structure. The team will test it by putting the structure in the freezer and observing it. Main point is that it should unfreeze before flight.

### Software

- What is the difference between test mode and normal mode? Timeline following? Yes, this is how the team understands it.
- Downlink is functional with the RXSM simulator, but uplink is having issues. Team will try to debug a bit but is recommended to reach out to SSC if they keep having issues.
- Team reverse engineered the firmware of the runcam to be able to delete the stored footage before flight. Not entirely sure yet if they will use this. Recording starts at SOE.
- Do you see if recording is working? TV multiplexer; in the video feed you can see that it's recording. SD card capacity is information sent to the GS and after a few seconds of recording you do see the capacity decrease.
- SD card capacity to be checked by the team with the flight recording settings.
- Table 4.30: SOE timing or "final power on + recording" of the camera ON. Consider that it may be nice to just have the experiment fully up and running before you give a "go" for flight, rather than turning subsystems off and on again very last minute.
- Table 4.30: power ON of experiments usually happens at T-10 minutes. If earlier is needed, this can be accommodated, but T-600s is custom. There will also be an "early check out" phase around T-2hours at which all experiments are turned on for a check-out.

### Verification & testing

- Have you already tested the multiplexer? Yes, with 2 cameras. They will do a test with all cameras but not planned yet.
- Most functional requirements are usually also verified through test. For example, when you are performing tests on the performance of certain sensors, you are also automatically crossing off certain functional requirements (F4-8).
- Would you want to verify (a part of) your pressure sensor requirements through test?
- D.3 (size measurement) is officially a test, not inspection.
- Number of items in the verification matrix have a status of "Due end June / July" but still status pending. Update timeline where needed.
- Vacuum tests for test chambers to determine leak tightness also included in M2. Recommended to split up the tests more.
- Thermal test planned after arrival of all runcams and the bulkhead (p.101). Is this still part of test M2 (Vacuum camera test)? Is it then completed? Is there any other different variable besides number of cameras? That is mostly it, LED temperatures can be re-checked again. The 5V regulator was not really loaded during the previous tests, so this should be included in a vacuum test to observe thermal behaviour as well.
- Team has very easy access to a vacuum chamber.

---

## Follow up of RIDs

- See updated RID matrix

---

## Hardware demonstration

### Hardware present

- 3x chambers; one with most recent design of endcaps.
- 4x camera holders + 4x DVR holders
- 3x chamber PCBs with LEDs and pressure sensors
- 1x main PCB v2 (functional)
- 1x multiplexer v1 (v2 in progress, mainly updated connectors)
- Cabling between main board + chamber PCBs nicely outfitted, as well as between 2x cameras and multiplexer (missing final connectors).

### Demonstrated

- IMU and pressure sensor functional
- 2 cameras functional, switching between cameras through multiplexer works
- Camera stream over video channel works
- LED brightness is adjustable
- "Testing" GUI is working well, will not be the flight GUI
- SOE and LO signals work
- Downlink is working through RXSM (not demonstrated) but team is having issues with uplink. Team can write to SSC if they are still having issues, we can support in debugging. Current communication over USB.
- Team tests at 20V now (rating for the RunCam), they are conservative in testing at 28V in case there are any issues with the buck converter, to not damage the runcams. They have tested before at 28V.

Question from the team on whether adding a sleeve around the cabling is beneficial. Recommended to first perform cable routing. Shielded and grounded sleeve may be useful to reduce noise. Recommended to perform noise test (peak to peak voltage should be >100mV).

LAM assembly: may not be rigid. Still needs to be ordered. Recommended to proceed soon with LAM ordering and integration.

*Figure 2: Hardware Demonstration Set-up Overview*
*Figure 3: Experiment Set-up*
*Figure 4, 5: Close-up Test Chamber and Endcap*
*Figure 6: Different Endcap Designs*
*Figure 7: Camera Stream Over Analog Channel*
*Figure 8: Test Chamber PCB Including Pressure Sensor and LEDs*
*Figure 9: Camera Support Including 2x Runcams*
*Figure 10: Test Chamber PCB Including LEDs (On)*
*Figure 11: Cameras (In Support) + DVRs (x2)*
*Figure 12: Multiplexer PCB*
*Figure 13: Main PCB (v2)*
*Figure 14: Test GUI*
*Figure 15: Mock-up Module*
*Figure 16, 17: Spare Parts (PCBs, Runcams, SD Cards)*

---

## Vacuum leak test of end cap and test chamber seal

The team performed a vacuum test of the latest endcaps with a single 3-mm O-ring on each endcap with improved groove dimensions. From preliminary results based on visual inspection of the dyed liquid, no leaks were present, a promising result! The team will continue testing the configuration with also pressure sensors installed in the test chambers.

*Figure 18, 19: Test Chamber Including Single 3mm O-ring (Per Endcap) and Coloured Liquid Dye for Leakage Observation*
*Figure 20: Sealing Set-up: Grease, Endcaps, O-rings (Putty for PCB Holes)*
*Figure 21: Sealed Endcaps*
*Figure 22: Test Chamber in Vacuum Chamber*
*Figure 23: Vacuum Chamber*

---

## Review of project planning

- **Team**
  - Team consists of 6 members; one is less available and will be back in 2 weeks.
- **Budget**
  - OK, some PCBs still to be manufactured but this is sponsored. Smaller items to be bought: 3D printing filament, stand-offs, etc. The main costs are travel costs, primarily for the campaign.
  - Sponsorship of cameras, very nice!
- **Schedule**
  - Next REXUS/BEXUS events:
    - EAR -> Tue 10 of November
    - ITW -> 7-11 December 2026. Note: experiments are shipped from ITW to BT by organisers. Recommended to take your own analog monitor. SSC to-do: check with ZARM if the vibration test can be including water.
    - Bench test -> 18-22 January 2026. Note: experiments prepared for spin & balance. Should dummy masses be at the end of the experiment chamber or at a certain position? SSC to-do: confirm mass dummy positioning.
    - Launch campaign -> 1-13 March 2025 (to be confirmed)

---

## Review of accommodation

- Including discussion of neighbouring experiments / rocket systems
- Placement in the rocket
  - Cabling: bulkhead cutout and DSUB bracket
  - Between RXSM and MARA
  - Fluid absorption pads: below MARA or on the LAM brackets, to be confirmed by the team.
- DSUB looks good, stand-offs present, BNC still missing but allocated.
- May be nice to have an angled BNC connector for easier assembly. SSC to-do: check with ZARM on part number of the angled connector and confirm that it can be used.
- Allocation of power and downlink: OK.

---

## Launch campaign discussion

### Safety discussion

- No major safety risks present in the experiment.
- Leakage risk: LAM is discussed (see earlier sections)

### Launch site requirements

- In general, include all your requests for the launch campaign in the Chapter 6.
- SSC to-do: Crimp tool for molex connectors (microfit 3.0), can it be provided?
- Consumables such as spare screws etc. are not provided.

### Preparation

- Preparation actions:
  - You should start planning for the whole campaign
  - Develop the activities you need to perform during the campaign:
    - Develop and practice assembly procedures
    - Develop and practice operational procedures ("check out" of the experiment, what steps do you take?).
  - Open ACTION 74 (CDR): add timing in table 6.7
- Mass dummies for S&B to be prepared and installed by the end of bench test.
- Intended test configuration(s) for ITW, Bench test (@Obf.), Bench test (@LC). No specific test configurations, all systems will run fully during all test cycles.
- Preparation: coating application after BT, before flight assembly. Timing needs to be clarified in SED v4-0: how much time do you required after the final bench test and before being flight ready on campaign? To disassemble, apply coating, fill, reseal, etc.

### Timeline

- Fluid filling on campaign: can be 3-4 days before launch. Team will evaluate more the effect of partial submersion (or short duration submersion + longer time dry) on the coating.
- LO / SOE: OK, but recommended to have the SOE signal a little bit earlier, such that you don't have any changes in the system or off/on before or after the "go" for flight.
- ACTION: Please specify in table 6.3 (electrical interfaces) also the time at which SOE shall be given, and when you want to have the TV channel on during pre-flight testing (in case of specific timing).

### Photography permission

- Not applicable, cameras not looking outside the rocket.

---

## Review wrap-up & Q/A

### Summary of review

- Almost all actions from CDR fully implemented. Only minor design changes since CDR.
- Nearly all hardware present and integrated: test chambers, camera supports, DVR boxes, electronics box and standoffs, cabling, Main board v2, multiplexer v1, chamber PCB v2 (x3).
- Successful hardware and functionality demonstration:
  - Cameras power on and stream over analogue TV channel
  - Test chamber PCB functioning, pressure sensor data visible in test GUI, LED brightness adjustable from test GUI
- Experiment and team are currently at a great status for IPR. Keep up the good work!

### Key next steps

- Vacuum tests of new chamber sealing design, + integration and sealing of chamber PCBs
- Thermal test of integrated system + camera subsystem
- Freezing test of liquid and structures
- Coating test in fluids: partial submersion and other variable conditions
- Communication to GS + GUI via RXSM simulator
- LAM ordering + integration
- Manufacturing of multiplexer board v2
- Main board v3 including added robustness/redundancy (optional)

### Q&A

- Shielding: perhaps a thin plate on the top/bottom of the E-Box would work better than aluminium tape (and may be easier to manufacture/integrate too).
- LAM: can they mount around the cable gutter? The cable gutter should in principle be open, to prevent snagging of the cables / DSUBs on the LAM when feeding them through. But having LAM on the "sides" of the cable gutter, oriented upwards, would be OK and would aid in preventing water from the experiment going down through the cable gutter.
- Mounting torques for experiment screws: the bolts mounting the bulkhead into the module are M5x10-12mm bolts, steel galvanized 8.8 or stainless steel A2/A4. They should be torqued to 5.4Nm. The bolts mounting the DSUB bracket into the module wall are 3x M4x8-10mm, stainless steel A2/A4 bolts. They should be torqued to 3.0Nm.

---

## After the review, IPR decision & next steps

RX/BX organisers decision (checkboxes not marked in the source):

- [ ] PASS
- [ ] CONDITIONAL PASS
- [ ] FAIL

Next steps definition with due date:

| Actions | Team / SSC / ESA | Due date | Comments |
|---|---|---|---|
| Catch-up | | Mon 26-10 | |
| SED v4-0 | | Sun 8-11 | |
| EAR | | Tue 10-11 | |
