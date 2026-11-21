# Mafia III — paket izvornog koda za Codex

Ovaj paket sadrži **naš eksperimentalni izvorni kod** iz grane
`feature/f8-fsr-menu-experimental` i detaljan Codex prompt za
**nativni AMD FSR 4.1.2 u originalnom F8 meniju Modern Tweaks 2.5.1**.

Nije u pitanju izvorni kod Modern Tweaks autora — originalni mod nije
objavljen uz svoj izvorni kod u dostavljenoj arhivi.

## Kako da koristiš

1. Raspakuj ZIP i otvori folder projekta u Codex-u ili editoru.
2. Otvori `codex_handoff/CODEX_PROMPT_NATIVE_FSR4_MAFIA3.md`.
3. Nalepi **celokupan tekst** prompta u Codex i priloži ovaj folder
   (ili mu daj pristup GitHub repozitorijumu/grani).
4. Kod za F8 i početni DX11 hook nalazi se u `f8_bridge/`.
5. Kod za AMD FidelityFX SDK 2.4 i DX11→DX12 bridge je u `probe/`.
6. Dokazni materijal i statički nalazi su u `docs/`, provere u `tools/`,
   a komande za build u `.github/workflows/`.
7. Ako originalni Modern Tweaks `dinput8.dll` treba za binarnu analizu,
   dodaj **svoju lokalnu kopiju** u Codex workspace kao zaseban fajl.
   **Nemoj** je objavljivati ili predstavljati kao naš izvorni kod.

## Ograničenja

- Trenutni F8 dodatak ume da prikaže FSR stavke, ali one **nisu aktivan FSR**.
- Samostalni AMD FSR 4.1.2 GPU testovi su rađeni na sintetičkim frejmovima.
- Nedostaje pouzdano preuzimanje *stvarnih* raw motion/depth/camera/jitter
  resursa i povezivanje sa pre-HUD TAA izlazom igre.
- FSR Quality/Balanced/Performance zahtevaju stvarno smanjenje interne 3D rezolucije.
- Zbog ranijih regresija **ne koristiti Lumu ni ReShade**.
- Prethodni Luma pokušaj uklonio je FSR stavke iz F8 menija i prijavljeno
  je podrhtavanje oblaka. Ne ponavljati tu implementaciju.

## Poželjni završni fajlovi od Codexa

- Kompajlirani `dinput8.dll` ili bezbedan kompatibilni loader,
  `mafia_f8_experimental.dll` / integrisani F8 interfejs,
  `mafia_live_fsr_backend.dll` i potrebni potpisani AMD SDK runtime fajlovi.
- `install.cmd`, `restore.cmd`, odgovarajući `.ps1` skriptovi i README.
- Dokazani build/test rezultati, log aktivnog providera i obrađenih pravih frejmova.
- ZIP spreman za testiranje na Windows 11/x64, uz jasan status koji FSR režimi
  **zaista** rade.

Codex mora da proveri status i da ne prikazuje dijagnostički DLL kao završen mod.

GitHub: https://github.com/lukalazicsrb-eng/Mafia-3-ultimate-tweaks
Native PR: https://github.com/lukalazicsrb-eng/Mafia-3-ultimate-tweaks/pull/1
