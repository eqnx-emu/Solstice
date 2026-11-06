# Legal

> [!CAUTION]
> This documentation is for informational purposes only and is not intended as legal advice. If you have
> any legal questions or concerns about using Sunshine, we recommend consulting with a lawyer.

Sunshine is licensed under the GPL-3.0 license, which allows for free use and modification of the software.
The full text of the license can be reviewed [here](https://github.com/LizardByte/Sunshine/blob/master/LICENSE).

## Commercial Use
Sunshine can be used in commercial applications without any limitations. This means that businesses and organizations
can use Sunshine to create and sell products or services without needing to seek permission or pay a fee.

However, it is important to note that the GPL-3.0 license does not grant any rights to distribute or sell the encoders
contained within Sunshine. If you plan to sell access to Sunshine as part of their distribution, you are responsible
for obtaining the necessary licenses to do so. This may include obtaining a license from the
Motion Picture Experts Group (MPEG-LA) and/or any other necessary licensing requirements.

In summary, while Sunshine is free to use, it is the user's responsibility to ensure compliance with all applicable
licensing requirements when redistributing the software as part of a commercial offering. If you have any questions or
concerns about using Sunshine in a commercial setting, we recommend consulting with a lawyer.

## Third-Party Notices

### SudoVDA

The virtual display backend uses the SudoVDA driver interface headers (`third-party/sudovda`), created by
[SudoMaker](https://github.com/SudoMaker/SudoVDA) and taken from the
[Apollo](https://github.com/ClassicOldSong/Apollo) project (GPL-3.0). The backend in
`src/platform/windows/virtual_display.cpp` is adapted from Apollo's SudoVDA integration.

The Windows package also bundles the SudoVDA driver binaries (`drivers/sudovda`). SudoVDA is based on the Microsoft
IndirectDisplay sample and is licensed by its author under "MIT and CC0 or Public Domain ... choose the least
restrictive option" (its repository ships no separate license file; see `src_assets/windows/drivers/sudovda/LICENSE`).
The driver is installed with [nefcon](https://github.com/nefarius/nefcon) (`nefconc.exe`, MIT License).
