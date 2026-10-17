# Sounds

Recordings by professional sound recordists and designers, from the Sonniss GDC Game Audio Bundles (free to use in a game; not to be
passed on as sound files; not for training AI). Chosen and cut by `Tools/audio/pro.py`, which also says why each was chosen.

| Sound | Recording | Library | Bundle |
|---|---|---|---|
| `loop_jet` | Gas Burner Stereo 4.wav | Digital Rain Lab - Driving Forces | 2017 |
| `loop_jet_boost` | AEROJet_Blast Off Clean_344 Audio_Air Designed.wav | 344 Audio - Air Designed | 2026 |
| `jet_ignite` | OLD STUFF_Water Heater_01.wav | GainWalkers - Old Stuff | 2018 |
| `jet_stop` | OLD STUFF_Water Heater_01.wav | GainWalkers - Old Stuff | 2018 |
| `jet_empty` | Bluezone_BC0268_switch_button_click_ignition_002_03.wav | Bluezone - Switch And Button Sound Effects | 2020 |
| `loop_wind` | 38 Wind in grass_Velebit mountain.WAV | Ivo Vicic - Natural ambiences vol.1 | 2019 |
| `loop_rush` | Skydiver - ONBRD - FLYING - Exit Plane Jump and Flying 2 - Chest - MKH8020.wav | Pole Position - The Skydiving and Parachute Library | 2024 |
| `loop_sea` | WATRWave_Medium Waves at Pebble Beach_JSE_RCoN_Stereo.wav | Just Sound Effects - Rocky Coast of Norway | 2026 |
| `loop_stream` | Stream - LIGHT - Medium Speed - Flow - Left-Heavy Trickles.wav | Pole Position - Winter Forest Stream | 2024 |
| `loop_star` | DSGNDron_Slow Pulse Drone_JSE_CD.wav | Justsoundeffects - Cinematic Drones | 2023 |
| `loop_music_day` | Pad 3.wav | 344 Audio - Mind States | 2023 |
| `loop_music_night` | Bluezone_BC0285_afterlife_ambient_synth_melodic_texture_008.wav | BluezoneCorp - Afterlife - Ethereal Ambient Samples | 2023 |
| `loop_music_space` | Space_Ambiences_07.wav | Fox Audio Post-Production - Space Ambiences - Drones & Atmospheres | 2018 |
| `awe` | Tonal Swell_01.wav | SoundFxwizard - SMOOTH Light Touch Production Elemetns | 2020 |
| `mote_ground_01` | Crystal, Glass, Tall Narrow, Tone, Rubbing, Musical_01.wav | Raconteur Sound - Crystal Glasses- Tones and Drones	 | 2017 |
| `mote_ground_02` | Crystal, Glass, Tall Narrow, Tone, Rubbing, Musical_05.wav | Raconteur Sound - Crystal Glasses- Tones and Drones	 | 2017 |
| `mote_ground_03` | Piano_MalletStrike_Metal_Damped_G_Full.wav | InspectorJ - Piano Mallet Strikes and FX | 2019 |
| `mote_ground_04` | SINGING_BOWL_Strike_Sustain_D_02.wav | Prismo Sound - Singing Bowls | 2019 |
| `mote_space_01` | Crystal, Glass, Red Wine, Tone, Rubbing, Musical_07.wav | Raconteur Sound - Crystal Glasses- Tones and Drones	 | 2017 |
| `mote_space_02` | Crystal, Glass, Huge, Tone, Rubbing, Musical_04.wav | Raconteur Sound - Crystal Glasses- Tones and Drones	 | 2017 |
| `mote_space_03` | SINGING_BOWL_Strike_Sustain_D_02.wav | Prismo Sound - Singing Bowls | 2019 |
| `voice_space_01` | Whale Call Low (echo) 9.wav | Varazuvi - Underwater Whale | 2015 |
| `voice_space_02` | Whale Call-Answer Close (echo) 7.wav | Varazuvi - Underwater Whale | 2015 |
| `voice_space_03` | Whale Call High (echo) 14.wav | Varazuvi - Underwater Whale | 2015 |
| `fly_on` | Whoosh,Sound Design,Airy,Full,Soft,Buffet,Medium.wav | Airborne Sound - Airy Whooshes | 2019 |
| `fly_off` | Whoosh_Source_Rope_03.wav | SoundBits - Tiny Transitions | 2018 |

Footsteps, jumps, landings, the lamp's switch and the menu's clicks are earlier CC0 recordings (Kenney, Fantozzi, Corsica_S,
MentalSanityOff, rubberduck and Peludo on OpenGameArt), made into takes by `Tools/audio/make.py`.

## Footsteps and movement

The layered footsteps, cut from professional foley walks by `Tools/audio/steps.py` (same bundles, same licence): `step_<surface>_<tier>_<foot>_NN` (impacts) and `roll_<class>_<tier>_NN` (rolls). Each surface, why, and where a substitute stands in:

- `moss`: A soft, low, damp thud: The Sound Pack Tree's walk in sneakers on carpet (its duller half: the thud, nearly nothing above 4 kHz), Tovusound's foley walk on grass (the damper half of its steps) and RYK's steps in mud, all closed at 3.5 kHz, with a bare-foot thump under (Joshua Reinhardt). Moss is quiet and wet (brief 7.1); no dry grass.
- `sedge`: Turf with sedge in it: the brighter half of the same carpet and grass walks, closed at 4.5 kHz (a little more blade than the moss, still no crinkle), with the same bare-foot thump under. The dry-grass singles that stood here crinkled.
- `ash` (SUBSTITUTE): there is no recording of dry black sand or ash. PMSFX's dirt-and-gravel walk, the dirt-dominated steps (the duller 55%), closed at 3.5 kHz and made 2.5 semitones lower, as the brief's section 7.2 prescribes.
- `gravel`: PMSFX 'Steps: dirt and gravel' (the brighter steps of its walk and two singles) and Studio 23's loafers on gravel.
- `basalt` (SUBSTITUTE): there is no recording of footsteps on bare basalt. Boots on clean hard ground, the nearest in the bundles (Joshua Reinhardt's walk; Tovusound's asphalt walk and run; Levan Nadashvili's firm-ground and crouched steps; brief 7.4).
- `snow`: Wav Junction, Studio 23, Pole Position, Soundrangers, Alexander Fokshtein, Bluezone, Soundopolis and Gamemaster: crusty, fresh and icy snow, walked and run (brief 7.6).
- `water`: Wav Junction's single steps into a puddle, with Bluezone's small splash, Soundrangers' heavy watery splat and Hzandbits' plop and slosh for the hurried steps (brief 7.11). Few real shallow-water steps exist; this pool is the smallest.

| Sound | Recording | Library | Bundle |
|---|---|---|---|
| `step_moss_*` and its rolls | Bare_Feet_Grass_RUN.R.wav | Joshua Reinhardt - Ultimate Bare Feet Expansion | 2018 |
| `step_moss_*` and its rolls | 1806 - Footsteps - Sneakers on Carpet - 90 fpm - Loop.wav | The Sound Pack Tree - Footstep Loops | 2018 |
| `step_moss_*` and its rolls | 169_Foley_Footsteps_Grass_Sneaker_Walk_Fast_Run_Jog_Close.wav | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps | 2017 |
| `step_moss_*` and its rolls | mud 1 loop.wav | RYK-Sounds - Footstep | 2023 |
| `step_sedge_*` and its rolls | Bare_Feet_Grass_RUN.R.wav | Joshua Reinhardt - Ultimate Bare Feet Expansion | 2018 |
| `step_sedge_*` and its rolls | 1806 - Footsteps - Sneakers on Carpet - 90 fpm - Loop.wav | The Sound Pack Tree - Footstep Loops | 2018 |
| `step_sedge_*` and its rolls | 169_Foley_Footsteps_Grass_Sneaker_Walk_Fast_Run_Jog_Close.wav | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps | 2017 |
| `step_ash_*` and its rolls | PM_SDNG_Stereo_Walk_Seamless_Loop_1.wav | PMSFX - STEPS Dirt & Gravel | 2019 |
| `step_gravel_*` and its rolls | PM_SDNG_Stereo_Walk_Seamless_Loop_1.wav | PMSFX - STEPS Dirt & Gravel | 2019 |
| `step_gravel_*` and its rolls | S23_SFX_Footsteps_Gravel_Loafers_Loops_Walk_Normal.wav | Studio 23 - Ultimate Footstep Collection | 2019 |
| `step_gravel_*` and its rolls | PM_SDNG_Single_Step_Footstep_19.wav | PMSFX - STEPS Dirt & Gravel | 2019 |
| `step_gravel_*` and its rolls | PM_SDNG_Single_Step_Footstep_46.wav | PMSFX - STEPS Dirt & Gravel | 2019 |
| `step_basalt_*` and its rolls | BOOT_CONC CLEAN_WALK.R.wav | Joshua Reinhardt - Ultimate Bare Feet Expansion | 2018 |
| `step_basalt_*` and its rolls | 015_Foley_Footsteps_Asphalt_Boot_Walk_Fast_Run_Jog_Close.wav | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps | 2017 |
| `step_basalt_*` and its rolls | FS Ground Civilian Walk N03.wav | Levan Nadashvili - Civilian Footsteps | 2016 |
| `step_basalt_*` and its rolls | FS Ground Civilian Walk N05.wav | Levan Nadashvili - Civilian Footsteps | 2016 |
| `step_basalt_*` and its rolls | FS Concrete Soldier Crouch N02.wav | Levan Nadashvili - Soldier Footsteps | 2016 |
| `step_basalt_*` and its rolls | FS Concrete Soldier Crouch N05.wav | Levan Nadashvili - Soldier Footsteps | 2016 |
| `step_snow_*` and its rolls | 0002_Footsteps_Walking_Crusty_Snow_Male_Shoes_Normal_Pace.wav | Wav Junction Sound Effects - Footsteps | 2020 |
| `step_snow_*` and its rolls | S23_SFX_Footsteps_Snow_Singles_Sequence.wav | Studio 23 - Ultimate Footstep Collection | 2019 |
| `step_snow_*` and its rolls | snow_foley_walking_on_snow_02.wav | Pole Position Production - The Snow & Ice Textures Library | 2016 |
| `step_snow_*` and its rolls | footstep_multiple_snow_boot_run_01_var03.wav | Soundrangers - Foley Elements Ice and Snow | 2018 |
| `step_snow_*` and its rolls | Snow Footstep Light Slow.wav | Alexander Fokshtein - Snow Steps | 2018 |
| `step_snow_*` and its rolls | Ice Footsteps Hard Packed.wav | Alexander Fokshtein - Snow Steps | 2018 |
| `step_snow_*` and its rolls | Bluezone_BC0264_snow_footsteps_walk_light_008.wav | Bluezone - Snow Footsteps | 2020 |
| `step_snow_*` and its rolls | Bluezone_BC0264_snow_footsteps_walk_013.wav | Bluezone - Snow Footsteps | 2020 |
| `step_snow_*` and its rolls | Bluezone_BC0264_snow_footsteps_walk_deep_008.wav | Bluezone - Snow Footsteps | 2020 |
| `step_snow_*` and its rolls | Bluezone_BC0264_snow_footsteps_run_004.wav | Bluezone - Snow Footsteps | 2020 |
| `step_snow_*` and its rolls | Footsteps_Icy_Normal_Single_Fienup_006_SIS2.wav | Soundopolis - Icy Snow 2 HD | 2019 |
| `step_snow_*` and its rolls | footstep_ice_crunchy_run_01.wav | Gamemaster Audio - Footstep and Foley Sounds | 2018 |
| `step_snow_*` and its rolls | footstep_ice_crunchy_run_02.wav | Gamemaster Audio - Footstep and Foley Sounds | 2018 |
| `step_water_*` and its rolls | 0014_Footsteps_water_puddle_single_splashes.wav | Wav Junction Sound Effects - Footsteps | 2020 |
| `step_water_*` and its rolls | Bluezone_BC0256_water_splash_small_008.wav | Bluezone - Splash | 2020 |
| `step_water_*` and its rolls | mud_splat_heavy_03.wav | Soundrangers - Hydrology Bubbles and Splashes | 2018 |
| `step_water_*` and its rolls | LIQMvmt_Liquid Movement Plop Slosh Single 07_HZBIT_Microbes_MKH60.Wav | Hzandbits - Microbes | 2023 |

Rolls are cut from the same steps (from the soft class: moss and sedge; grit: ash, gravel, basalt; snow: snow).
The older `step_<surface>_NN`, `land_NN` and `suit_step_NN` files are the previous cut of the same recordings and stay until the game reads the new keys.

## Music

By Scott Buckley, released under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/), www.scottbuckley.com.au. Laid out by `Tools/audio/score.py`.

- 'Borealis' by Scott Buckley (`loop_music_day`): "A contemplative, slow-moving ambient track, with slight uplifting tone. Inspired by the quiet stillness of the boreal forests."
- 'Permafrost' by Scott Buckley (`loop_music_day`): "A meditative, ebbing ambient work for strings, synth and solo violin." The evenest of all twelve measured: it never rises more than a decibel.
- 'Shadows and Dust' by Scott Buckley (`loop_music_night`): "An ethereal, enigmatic ambient track featuring synth drones and winds, blended with strings and synth textures."
- 'The Long Dark' by Scott Buckley (`loop_music_night`): "A melancholic ambient piano track with strings and atmospheric synth": the long cold darkness of a northern winter.
- 'Decoherence' by Scott Buckley (`loop_music_space`): "An atmospheric, haunting ambient work featuring slowly morphing synths, ominous strings and solo viola. Perfect for deep space travel."
- 'Adrift Among Infinite Stars' by Scott Buckley (`loop_music_space`): "A contemplative, minimalist piece for piano, synths, strings and solo violin": for looking up at the evening sky.

## Foley library

The cloth, gear, vegetation, debris, water, breath and movement-event sounds of the layered step system, cut and laid together by `Tools/audio/foley.py` (same bundles, same licence). Every one is made of real recordings, listed here; nothing is synthesised. The events (`start_`, `settle_`, `turn_`, `skid_`, `takeoff_`, `land_`) are also made of the steps and rolls of `steps.py`.

| Group | Recording | Library | Bundle |
|---|---|---|---|
| cloth | FOLYClth_ClothMovement24_InMotionAudio_FoleyT-Shirt.wav | InMotionAudio - Foley T-Shirt | 2026 |
| cloth | FOLYClth_ClothMovement29_InMotionAudio_FoleyT-Shirt.wav | InMotionAudio - Foley T-Shirt | 2026 |
| cloth | FOLYClth_SinglePats04_InMotionAudio_FoleyT-Shirt.wav | InMotionAudio - Foley T-Shirt | 2026 |
| cloth | CLOTHING_MATERIAL_MOVEMENT_08.wav | Shapeforms - Hit & Punch | 2020 |
| cloth | shake_clothing_001.wav | 3maze -  The Closet | 2016 |
| cloth | Scrape_Cloth_10.wav | SoundBits - Tiny Transitions 2 | 2019 |
| cloth | CLOTHFlp_Action Inventory Open Flip Cloth Canvas Bag Slide Light 02_ESM_FG2.wav | Epic Stock Media - Fantasy Game 2 - Sound Kit for Enchanted Realms | 2026 |
| cloth | Blanket-Lift_06.wav | SoundBits - Open & Close 2 | 2018 |
| cloth | person_removing_jacket_001.wav | 3maze -  The Closet | 2016 |
| cloth | Whoosh_Cloth_Leather_Fight_174.wav | SoundBits -  Just Whoosh 3 _ Whoosh Essentials | 2016 |
| cloth | LeatherArmor_Rustle_03.wav | Double Trouble Audio - Medieval Armor and Impacts | 2017 |
| cloth | HERO - CK - ARMOR Leather Walk.wav | Rock The Speakerbox - Hero | 2019 |
| gear | SBvfe1_Jiggling Belt Buckle 004.wav | Sonic Bat - Videogame Foley Essentials Vol. I | 2023 |
| gear | EQUISprt_Climbing Gear, Carabiner, Screwing Lock 01_344 Audio_Climbing Gear Foley Vol 1.wav | 344 Audio - Climbing Gear Foley Vol. 1 | 2026 |
| gear | foley_soldier_gear_equipment_metal_cloth_heavy_movement_light_08.wav | Gamemaster Audio - Footstep and Foley Sounds | 2018 |
| gear | PR LEATHER BACKPACK_JOG_416.L.wav | Joshua Reinhardt - Ultimate Cloth and Prop Collection | 2017 |
| vegetation | RUSTLE Studio performed green leaves twigs brush moves.LR.wav | Articulated Sounds - Rustle Tones | 2018 |
| debris | Tatak_ROCKS_Single Rock Impact on Stones.wav | Tatak Audio - Rocks and Debris | 2020 |
| debris | Rock Hit and Bounce Dirt_RBDII 06.wav | Mechanical Wave - Rock,Brick and Dirt - Part 02 | 2017 |
| debris | Hit Rock into Gravel_RBDII 06.wav | Mechanical Wave - Rock,Brick and Dirt - Part 02 | 2017 |
| debris | SBvfe2_Medium Rock Dropping 011.wav | Sonic Bat - Videogame Foley Essentials Vol. II | 2024 |
| debris | Small Crushed Rock - DROP - Thick - Stereo.wav | Pole Position Production - The Stone & Sand Library | 2023 |
| debris | Tatak_ROCKS_Pebbles Dust Tumbling Rolling Debris.wav | Tatak Audio - Rocks and Debris | 2020 |
| debris | Scree Rock_RBD 06.wav | Mechanical Wave - Rock Brick and Dirt | 2015 |
| debris | Crumbling Rock Slide_RBDII 07.wav | Mechanical Wave - Rock,Brick and Dirt - Part 02 | 2017 |
| debris | ICEBrk_FROZEN FARMLAND Footstep on frozen field_Eneas Mentzel_ _39.wav | Eneas Mentzel - Frozen Farmlands | 2023 |
| debris | Ice,Creak,Sheet,Crunch,Step,Snow,Various10.wav | Mattia Cellotto - Crunch Mode | 2015 |
| debris | Ice,Impact,Throw,Snowball,Step,Snow,Stalactite,Various12.M.wav | Mattia Cellotto - Crunch Mode | 2015 |
| debris | Ice Footsteps Hard Packed.wav | Alexander Fokshtein - Snow Steps | 2018 |
| debris | Snow Footstep Light Slow.wav | Alexander Fokshtein - Snow Steps | 2018 |
| debris | Snow Slide Medium .wav | Alexander Fokshtein - Snow Steps | 2018 |
| water | 0014_Footsteps_water_puddle_single_splashes.wav | Wav Junction Sound Effects - Footsteps | 2020 |
| water | Bluezone_BC0256_water_splash_small_008.wav | Bluezone - Splash | 2020 |
| water | Bluezone_BC0256_water_splash_drop_006.wav | Bluezone - Splash | 2020 |
| water | mud_splat_heavy_03.wav | Soundrangers - Hydrology Bubbles and Splashes | 2018 |
| water | LIQMvmt_Liquid Movement Plop Slosh Single 07_HZBIT_Microbes_MKH60.Wav | Hzandbits - Microbes | 2023 |
| water | EFX EXT GROUP Marching Puddle Mud 01.wav | Coll Anderson - Battle Crowd | 2015 |
| water | Water_Splash_SeaLion_Fienup_001.wav | Soundopolis - Water 01 | 2015 |
| water | Water,Splashes,Bath Tub,Roomy,Dunk,Slow,Hard,x3.wav | Airborne Sound - Bathroom | 2018 |
| water | Glacial Lake - Large Size Rock Splashes - 5 Meters. Water,Splash,Large,Rock,Impact,Various_06.wav | Mattia Cellotto - Rocks Momentum | 2016 |
| water | Water,Inlet,Wash,Bubbling,Between,Rocks,Liquid,Bubbles,Loop_01.wav | Mattia Cellotto - Water Volumes | 2017 |
| water | LIQUID HIT Lake, Branch, Splash, Lake, Seaweed in Water, Small, CU 04.wav | Articulated Sounds - Magic Elements vol.2 | 2020 |
| water | Bluezone_BC0256_water_splash_008.wav | Bluezone - Splash | 2020 |
| water | Splash_Dive_Fienup_002.wav | Soundopolis - Water 01 | 2015 |
| water | UWM027.wav | TheWorkRoom Audio Post - Underwater Movement | 2023 |
| water | 24 Bubbles_regulator_submerged 4_stereo_AKG.wav | Ivo Vicic - Scuba Diving equipment and underwater diving | 2018 |
| water | UWM008.wav | TheWorkRoom Audio Post - Underwater Movement | 2023 |
| water | Underwater_Bubbles_Deep_Fienup_016.wav | Soundopolis - Underwater | 2015 |
| water | Underwater_Bubbles_Fienup_026.wav | Soundopolis - Underwater | 2015 |
| water | water_bubble_19.wav | Soundrangers - Hydrology Bubbles and Splashes | 2018 |
| extras | mud_splat_heavy_03.wav | Soundrangers - Hydrology Bubbles and Splashes | 2018 |
| extras | Bluezone_BC0261_mountain_dweller_creature_orc_footstep_mud_texture_003.wav | Bluezone - Mountain Dweller - Orc Sound Effects | 2020 |
| extras | Bluezone_BC0272_creature_texture_mud_006.wav | Bluezone - Cave Creature Sound Effects | 2020 |
| extras | LIQMvmt_Liquid Movement Plop Slosh Single 07_HZBIT_Microbes_MKH60.Wav | Hzandbits - Microbes | 2023 |
| extras | mud 1 loop.wav | RYK-Sounds - Footstep | 2023 |
| extras | Bluezone_BC0256_water_splash_drop_006.wav | Bluezone - Splash | 2020 |
| extras | cave_drips_05_30_loop.wav | Soundrangers - Hydrology Bubbles and Splashes | 2018 |
| extras | Water_Drain_Sewer_Fienup_005.wav | Soundopolis - Water 01 | 2015 |
| extras | WATRDrip_SingleDrip03_InMotionAudio_CaveDesign.wav | InMotionAudio - Cave Design | 2024 |
| extras | Rock Hit and Bounce Dirt_RBDII 06.wav | Mechanical Wave - Rock,Brick and Dirt - Part 02 | 2017 |
| extras | Hit Rock into Gravel_RBDII 06.wav | Mechanical Wave - Rock,Brick and Dirt - Part 02 | 2017 |
| extras | SBvfe2_Medium Rock Dropping 011.wav | Sonic Bat - Videogame Foley Essentials Vol. II | 2024 |
| extras | Tatak_ROCKS_Single Rock Impact on Stones.wav | Tatak Audio - Rocks and Debris | 2020 |
| extras | Scree Rock_RBD 06.wav | Mechanical Wave - Rock Brick and Dirt | 2015 |
| breath | HMNBrth_Human, Adult, Male, Breathe, Mouth, Slow,_UberDuo_INHL.wav | UberDuo - Inhale – Owen – Character Playset | 2024 |
| breath | HUMAN BREATH Male_Deep Mouth Inhale and Powerful Strong Exhale with Acceleration_A.wav | Articulated Sounds - Human Male Breathe | 2020 |
| breath | Male 1 - Breathing - Gasping - Ehrlund M1.wav | Pole Position Production - The Grunts Library | 2023 |
| breath | HUMAN BREATH Male_Deep Opened Mouth Breath with S Formed Lips Long Exhale_F.wav | Articulated Sounds - Human Male Breathe | 2020 |
| events | HeavyWood_Sliding_on_ForestFloor_01_04.wav | SoundBits - Drag & Slide | 2017 |
| events | rockslide_02.wav | Soundrangers - Foley Elements Rocks, Dirt And Stone | 2018 |
| events | Crumbling Rock Slide_RBDII 07.wav | Mechanical Wave - Rock,Brick and Dirt - Part 02 | 2017 |
| events | Scree Rock_RBD 06.wav | Mechanical Wave - Rock Brick and Dirt | 2015 |
| events | Snow Slide Medium .wav | Alexander Fokshtein - Snow Steps | 2018 |
| events | Whoosh,Organic,Fabric,Glove,Airy,Breathy,Punchy.wav | Airborne Sound - Organic Whooshes | 2019 |
| events | Whoosh_Cloth_Leather_Fight_174.wav | SoundBits -  Just Whoosh 3 _ Whoosh Essentials | 2016 |
| events | RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10.wav | Red Libraries - Bodyfall | 2019 |
| events | RL_bodyfall_Concrete_Generic_Feet_Mid_Mono_Med_Impact_02.wav | Red Libraries - Bodyfall | 2019 |
| events | ARMOR Body Drop Chain Leather Short 02.wav | SmartSoundFX – Medieval | 2020 |
| events | 289_Foley_Footsteps_Rocks_Sneaker_Jump_Land_On_Two_Feet_Close.wav | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps | 2017 |
| events | Snow Impact Stomp Hard.wav | Alexander Fokshtein - Snow Steps | 2018 |
| events | Bluezone_BC0256_water_splash_small_008.wav | Bluezone - Splash | 2020 |
| events | LIQUID HIT Lake, Branch, Splash, Lake, Seaweed in Water, Small, CU 04.wav | Articulated Sounds - Magic Elements vol.2 | 2020 |
| events | Bluezone_BC0256_water_splash_008.wav | Bluezone - Splash | 2020 |
| events | Splash_Dive_Fienup_002.wav | Soundopolis - Water 01 | 2015 |
