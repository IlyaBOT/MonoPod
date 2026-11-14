# R2 electrical comparison

Primary: https://www.nxp.com/docs/en/data-sheet/TDA1308.pdf , Rev.5, Fig.4 p.6; pinout p.3; characteristics p.5; SO8 p.9.

Retained: pinout 1 OUTA / 2 INA- / 3 INA+ / 4 GND / 5 INB+ / 6 INB- / 7 OUTB / 8 VDD. Divider 10k+10k, CREF 22u, bulk 100u, bypass 100n, inverting channels with Rin=Rf=3.9k, output electrolytic + toward driver.

R2 retains every analog R1 value R1-R14 and C1-C7. R15-R20 link footprints are replaced with four three-pad open solder selectors. Default ATT closes 1-2; DIRECT closes 2-3 on both selectors of a channel and isolates the unused divider at both ends. All ceramics are now leaded; electrodes and values unchanged. Existing 2.2R output isolation and 100k bleeds retained. Copper is only on B.Cu; SMD on bottom, THT capacitors on top. The 100n capacitor has a short connection to pin8 and trimmed leads are required.

This is static/CAD verification, not physical stability, THD or output-power verification. The single-side layout, unknown salvaged IC and real headphone cable must be checked on the bench as documented. No extra output capacitive load or uncalculated RC filter was added.
