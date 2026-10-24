# Third-party work

The Visual FX page's COLOUR and PATTERN rows port other people's screen shaders to the CPU. The port is `overlay/minui_shader.{h,cpp}`, GPL-3.0-or-later like the rest of the DSperate binary it is built into (`LICENSE.DSperate`). These are the shaders it comes from, who wrote them and under what licence.

| Shader | Authors | Licence | Used for |
| --- | --- | --- | --- |
| The RG DS presets (`ds_classic`, `ds_lite`, `natural_glow`, `natural_smooth`, `vivid_lcd`, `vivid_smooth`, `pixel_beads`, `glossy_tiles`, `gba_color`, `gba_screen`, `soft_scanlines`, `soft_lcd_grid`, `warm_paper`, `cool_crisp`, `pocket_green`, `amber`) | [kleinrein/rgds-core](https://github.com/kleinrein/rgds-core) | GPL-3.0-or-later | Every COLOUR and PATTERN but the ones below |
| lcd1x | jdgleaver, from Gigaherz's lcd3x | GPL-2.0-or-later; lcd3x is public domain | PATTERN `lcd` |
| zfast_lcd | Greg Hogan (SoltanGris42) | GPL-2.0-or-later | PATTERN `zfast_lcd` |
| nds-color | hunterk, modified by Pokefan531 | Public domain | COLOUR `nds` |
| The GBA colour model | Pokefan531 | Public domain | rgds-core's `gba_color` and `gba_screen`, so COLOUR `gba` |
| sharp-bilinear | Themaister | Public domain | PATTERN `sharp_bilinear` |
| Natural Vision | ShadX, ported by Hyllian and parameterised by Sp00kyFox | None stated | COLOUR `natural_vision`. With no licence to go by, no code was copied: the maths (gamma 2.2, Y raised to 1.1 and I and Q scaled by 1.1 in NTSC YIQ, gamma 2.2) was written out afresh |

The GPL-2.0-or-later shaders are used under the "or later" terms, as GPL-3.0. The libretro originals are in [libretro/glsl-shaders](https://github.com/libretro/glsl-shaders). lcd1x and zfast_lcd are ported with the constants rgds-core gives them (`BRIGHTEN_SCANLINES 24`, `BRIGHTEN_LCD 6`, `BORDERMULT 16`).
