# Tesla Roadster Rocket: model size and wheel arches

Updated 17 October 2026. Both the showroom and racing models use the same
calibration against the stock Corvette ZR-1 and Acura NSX. The showroom uses
the stock 20-times coordinate scale, with additional precision for small details.
The earlier independent showroom enlargement is superseded.

## What is known about the 2026 car

The reported wheelbase is **2,825 mm** and width is **approximately 2,000 mm**.
These figures concern the redesigned car shown in October 2026, not the 2008
Roadster or the 2017 concept. They are reported from the Tesla executive
[Top Gear interview](https://www.youtube.com/watch?v=JdkIOhFmbKI), with the
wheelbase reproduced in [ElectronMotion's interview recap](https://electronmotion.com/2026/10/16/tesla-roadster-reviews/)
and width in [EVwire's interview summary](https://evwire.com/p/tesla-roadster-deep-dive-videos-hagerty-top-gear-motortrend).
The video page did not expose a transcript to independently check the spoken numbers.

No confirmed overall length or height was found in the current reveal sources.
The approximately **4.6 m length** used for calibration is an estimate from
the supplied photographs' model proportions and the reported wheelbase, not
a Tesla specification. Height is a photo-based estimate matched to the low
Corvette/NSX silhouette. The Rocket configuration is assumed to share the
underlying body's dimensions; no separate Rocket dimensional drawing was found.

## Stock reference measurements

Dimensions below are millimetres. Variant-specific uncertainty is retained;
this table does not imply that every game car represents one unambiguous year.
No stock car resource is changed.

| Game ID | Reference vehicle | Length | Width | Height | Wheelbase | Source |
|---|---|---:|---:|---:|---:|---|
| ANSX | 1991 Acura NSX | 4,405 | 1,810 | 1,170 | 2,530 | [Honda press kit](https://acuranews.com/en-US/releases/release-ff6633ebcc698302413a3d004c34ca74-1991-acura-nsx-specifications) |
| AUDI | Audi quattro 20V reference; game trim ambiguous | 4,404 | 1,723 | 1,320 | 2,524 | [Auto Zeitung](https://www.autozeitung.de/audi-quattro-20v-porsche-911-carrera-kaufberatung-vergleich-bilder-technische-daten-82507.html) |
| COUN | Countach LP5000 Quattrovalvole | 4,140* | 2,000 | 1,070 | 2,500* | [Specialist specifications](https://www.lambocars.com/countach-lp500-quattrovalvole-specs-performance/) |
| FGTO | Ferrari 250 GTO Series I | 4,325 | 1,600 | 1,210 | 2,400 | [ADAC](https://www.adac.de/rund-ums-fahrzeug/autokatalog/marken-modelle/ferrari/250-gto/1generation/350469/) |
| JAGU | Jaguar XJR-9 LM reference; game labels IMSA | 4,780* | 2,000 | 1,100 | 2,780 | [Ultimatecarpage](https://www.ultimatecarpage.com/spec/764/Jaguar-XJR-9-LM.html) |
| LANC | Delta HF Integrale 16V | 3,900 | 1,685 | 1,365 | 2,480 | [FIA homologation](https://historicdb.fia.com/sites/default/files/car_attachment/1601054101/homologation_form_number_5394_group_a.pdf) |
| LM02 | Lamborghini LM002 | 4,900 | 2,000 | 1,850 | 3,000 | [Manufacturer brochure](https://www.motorarkiv.dk/brochurer/LAMBO_LM002.pdf) |
| P962 | Porsche 962 IMSA/GTP reference | 4,770* | 2,000 | 1,013 | 2,770 | [Mulsanne's Corner](https://www.mulsannescorner.com/Porsche962.html) |
| PC04 | Porsche 964 Carrera 4 coupe | 4,250 | 1,652 | 1,310 | 2,272 | [Factory manual scan](https://www.scribd.com/document/759530585/Porsche-964-911-Carrera-Manuel-Technique-Notice-d-Utilisation-1989-1993) |
| PMIN | March-Porsche Indy reference, year unresolved | 4,660* | 2,010* | 980* | 2,800–2,850* | [Historical specifications](https://www.motor-car.net/porsche/item/23503-porsche-2708-cart) |
| VETT | 1990 Corvette ZR-1 | 4,506 | 1,880 | 1,189 | 2,443 | [ZR-1 registry](https://www.zr1netregistry.com/Information/C4ZR-1GeneralInfo/Dimensions.aspx), [Corvsport](https://www.corvsport.com/1990-corvette-specifications/) |

*Countach length and wheelbase differ across sources and equipment; 2,450 mm
is also reported for wheelbase. XJR-9 and 962 bodies vary with race configuration.
The exact March-Porsche chassis/year is unresolved. These entries provide context,
not the Tesla's calibration anchors. Audi's game name, engine and output also
mix identifiers, so the 20V reference is not asserted to be an exact trim match.

The stock meshes are stylized: inferred length scale ranges from roughly 40 to
54 mm per model unit. Even the Corvette and NSX differ in wheelbase scaling.
An exact real-world size ratio against every unchanged stock car is therefore
impossible. Corvette body size and the manufacturer-documented NSX wheelbase
provide the practical reference, with integer-coordinate rounding.

## Encoded result

| Model | Length | Width | Height including tyres | Wheelbase |
|---|---:|---:|---:|---:|
| Previous Tesla racing | 75 | 34 | 20 | 46 |
| Revised Tesla racing, both detail levels | 105 | 44 | 23 | 64 |
| Stock Corvette racing | 103 | 42 | 23 | 62 |
| Stock NSX racing | 100 | 40 | 24 | 58 |
| Revised Tesla showroom, divided by 20 | 105 | 45 | 23.3 | 64 |

These are model units, not millimetres. Sub-unit detail is retained in the
showroom and rounds to integers in racing. Pixel silhouettes also depend on
perspective and body shape, so a slightly longer mesh need not occupy more
horizontal pixels at every view angle.

The body axis multipliers from the original authored Tesla are approximately
1.324 in width, 1.20 in height and 1.40 in length. The wheel reduction target is
25% relative to the enlarged body: radius 6 × 1.40 × 0.75 = 6.3, rounded to
**6 game units**. The showroom uses the same wheel at radius 120. This is about
29% smaller than an unreduced 8.4-unit wheel, the closest whole-unit size to the
requested 25% reduction. It also keeps tyre bottoms on the original engine's
six-unit contact plane on every platform.

All four wheel openings are rebuilt as circular geometry in final model
coordinates, independently of body scaling. Each lip has nine symmetric
samples, continues below the axle, and uses at most 30-degree arc segments.
Concave body panels are triangulated so the original renderer does not fill
the openings. Both racing detail levels retain the same arch geometry.
The integer racing chords leave at least 0.79 unit of tyre clearance.

The SIMD wheel contacts, collision bounds and cockpit eye height follow the
larger racing model. Gearing, torque, adhesion flag and dashboard resources are
retained. Vehicle dimensions affect handling, so recordings should be played
with the same car resource version. Install **CARTERR.RES and STTERR.3SH together**.
No new engine behavior or command-line option is needed for these resource edits.
