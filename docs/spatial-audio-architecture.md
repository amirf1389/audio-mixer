# Spatial audio architecture — a comparison for AmF X1

*AmF X1 by AMFB Audio · 1.22.1.0 Beta · written 2026-11-08*

This note compares the main immersive / spatial audio systems and explains how AmF X1 relates to each. It was written to answer an
analysis request, so it is careful about what is **established** versus what is a **vendor claim this document has not independently
verified**. Read the "Confidence" column before quoting any number.

> **Trademarks.** Dolby Atmos, DTS:X, Sony 360 Reality Audio, Auro-3D, MPEG-H 3D Audio, THX, IMAX Enhanced and Eclipsa Audio are the
> trademarks of their respective owners. AmF X1 does **not** implement, decode, license or claim any of them. Where this document says
> AmF X1 "routes to" a format, it means AmF X1 sends ordinary PCM to the operating system's output and lets the OS / device do the
> spatial rendering — AmF X1 neither encodes nor badges itself with these names. **AMFB DNA** is AMFB Audio's own tuning profile (see
> below); it is not a licensed format and is not compatible or affiliated with any of the above.

## 1. Architectural comparison

Three broad architectures underlie almost every system:

- **Channel-based** — audio is mixed to a fixed speaker layout (5.1, 7.1, 7.1.4). Simple and predictable; it does not adapt to other
  layouts without downmixing.
- **Object-based** — sounds are stored as objects with position metadata, and a renderer places them on whatever speakers (or headphones)
  the listener actually has. Adapts to the room; needs a renderer at playback.
- **Scene-based (ambisonics)** — the whole sound field is stored as spherical-harmonic components, independent of any speaker layout, then
  decoded to the listener's setup. Natural fit for 360 / VR and for headphone binaural.

| System | Primary approach | Owner / steward | Licensing | Confidence | How AmF X1 relates |
|---|---|---|---|---|---|
| **Dolby Atmos** | Object-based (objects over a channel bed) | Dolby | Proprietary, licensed | Architecture well-established; object counts and bed sizes are vendor figures not verified here | Detect the OS "Dolby" spatial output if present and send PCM to it, untouched |
| **DTS:X** | Object-based | Xperi / DTS | Proprietary, licensed | Well-established | Same: route to the OS/device, do not decode |
| **Sony 360 Reality Audio** | Object-based, delivered over MPEG-H | Sony | Proprietary, licensed | Well-established that it is object-based on MPEG-H; delivery details are vendor-stated | Not rendered by AmF X1; route to the platform that supports it |
| **Auro-3D** | Channel-based, layered (height layer above the ear-level layer) | Auro Technologies | Proprietary, licensed | Well-established | Route to the OS/device if present |
| **MPEG-H 3D Audio** | Object + channel + scene (HOA) hybrid; an ISO/MPEG standard | ISO/IEC MPEG | Standard; implementations may carry patent licences | Well-established as a hybrid standard | Not rendered by AmF X1 |
| **THX Spatial Audio** | Headphone virtualization layer (not a delivery codec) | THX | Proprietary | THX is primarily a certification / virtualization brand, not a channel format | AmF X1's own crossfeed / 360 are the comparable in-app features; AmF X1 does not use the THX name |
| **IMAX Enhanced** | A certification bundle; its audio path has used DTS technology | IMAX + partners | Proprietary programme | IMAX Enhanced is a certification programme, not a standalone codec; the DTS association is vendor-stated | Not rendered by AmF X1 |
| **Eclipsa Audio / IAMF** | Scene + object, open and royalty-free | Alliance for Open Media (Google, Samsung, Arm and others) | **Open, royalty-free** | Confirmed open/royalty-free; consumer name announced at CES 2025; YouTube began IAMF upload support in 2025 (secondary sources) | The only one AmF X1 could render in-app in future, via the open **libiamf** reference library — research item, not shipped |
| **AMFB DNA (W-shape)** | AMFB Audio's own tuning profile: 4 raw EQ bands + 3 environmental parameters | AMFB Audio (this project) | The project's own | This project's own design | Built in; see §4 |

What this table is **not**: a claim that AmF X1 plays back any licensed format. AmF X1's own spatial features are the Resonance-based
**360 audio** binaural renderer (already shipped) and **headphone crossfeed** (1.22.1.0). Everything branded above is detected and handed
to the operating system, which is the only component licensed to render it.

## 2. The everyday living-room trade-off

For a normal living room with a soundbar or a TV:

- **Object-based (Atmos / DTS:X)** gives the best adaptation to an odd speaker layout, because the renderer in the soundbar places objects
  for your actual speakers. The cost is that both the content and the device must support the format, and content can be scarce or carry a
  licensing cost that reaches the listener as a pricier device or a subscription tier.
- **Channel-based (plain 5.1 / 7.1, Auro-3D)** "just works" when your speakers match the mix, and is cheap, but a hard-coded mix does not
  adapt — a 7.1.4 mix on a 2.0 soundbar is only as good as its downmix.
- **Open scene/object (Eclipsa / IAMF)** is the interesting middle: adaptive like object-based, but royalty-free, so a budget soundbar or a
  phone can include it without a per-unit licence. Its weakness today is simply maturity — content and device support are still growing.

For most people on headphones (the common case for a phone or a laptop), none of the above matters as much as a good **binaural** render
plus **crossfeed**, which is exactly what AmF X1 does in-app without any licence.

## 3. Creator vs enterprise

- **A creator** (one person, a pair of headphones, a stereo or small interface such as the Behringer UM2) mostly wants: a trustworthy
  stereo image, headphone crossfeed so hard-panned elements are not fatiguing, and a binaural 360 preview. Licensed object formats add
  cost and a mastering/encoding step that a solo creator rarely needs. AmF X1's crossfeed + 360 cover this.
- **An enterprise** (a broadcaster, a streaming platform, a cinema) needs an interchange format, certification, and guaranteed playback on
  certified hardware — which is where the licensed formats earn their fee, and where an open standard like IAMF/Eclipsa is attractive
  because it removes the per-unit royalty while keeping the adaptive renderer.

AmF X1 sits on the creator side and, for enterprise formats, stays a good citizen: it sends clean PCM and lets the certified renderer do
its job, rather than pretending to be one.

## 4. AMFB DNA (W-shape) — this project's own profile

AMFB DNA is **AMFB Audio's own** voicing, not a licensed format and not an imitation of one. It is built from:

- **4 raw EQ bands** — the project's house tuning curve (a "W" shape: lifted lows and highs with a controlled presence dip and lift), applied
  as ordinary parametric EQ on the master.
- **3 environmental parameters** — room size, distance and brightness, which scale the curve and the in-app ambience for the listening
  space. They drive only AmF X1's own EQ / width / crossfeed, nothing trademarked.

A **mid-bass multi-way option** (splitting the low end so the mid-bass is tuned separately) is planned and will be validated on the UMC404
simulator profile before it ships. It is listed here so the design is on record; it is **not** in 1.22.1.0 unless the changelog says so.

## 5. What is and isn't in AmF X1 today

| Feature | State in 1.22.1.0 Beta |
|---|---|
| 360 audio (Resonance binaural, azimuth/elevation/distance/spread/room) | **Shipped** |
| Headphone crossfeed (natural / warm / wide) | **Shipped (1.22.1.0)** |
| Detect the OS spatial output (Windows Sonic / Dolby / DTS) and route to it untouched | Planned (block 3 continues) |
| Height panning 2.0 → 9.1.6, Cinema DSP room presets | Planned |
| Eclipsa / IAMF in-app render via libiamf | Research only — depends on the open libiamf; nothing shipped |
| AMFB DNA W-shape, mid-bass multi-way | Profile described here; shipping status per the changelog |

## Sources

- [Google and Samsung — Eclipsa Audio (Android Central)](https://www.androidcentral.com/accessories/audio/google-samsung-come-together-to-introduce-eclipsa-audio)
- [Google, Spatial9 and Music AI debut Eclipsa Audio at CES 2025 (PR Newswire)](https://prnewswire.com/news-releases/google-spatial9-and-music-ai-debut-eclipsa-audio-immersive-format-at-ces-2025-302344338.html)
- [Arm — Eclipsa Audio with Google and Samsung](https://newsroom.arm.com/blog/eclipsa-audio-google-samsung-arm)
- [Google Open Source blog — IAMF](https://opensource.googleblog.com/search/label/IAMF)

*Architectural categories (channel / object / scene-ambisonics) are standard, well-established audio-engineering concepts. Specific vendor
numbers (object counts, bed channels, speaker maxima) are deliberately omitted or marked, because this document did not verify them against
the owners' own specifications.*
