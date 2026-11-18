/* SPDX-FileCopyrightText: 2026 ct-recomp contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the tile for the PS5 homebrew launcher (websrv, docs/PS5.md).
 * Lists the ROMs in this folder and starts eboot.elf with the one picked.
 */
async function main() {
    const DIR = window.workingDir;
    const PAYLOAD = DIR + '/eboot.elf';
    const ROMTYPES = ['sfc', 'smc'];

    async function romItems() {
        const listing = await ApiClient.fsListDir(DIR + '/');
        if (!listing.data) {
            throw new Error('Cannot list ' + DIR + ', error code: ' + listing.status);
        }
        return listing.data
            .filter(e => ROMTYPES.includes(e.name.slice(-3).toLowerCase()))
            .map(e => ({
                mainText: e.name.slice(0, -4),
                secondaryText: 'Start',
                onclick: async () => ({ path: PAYLOAD, args: DIR + '/' + e.name }),
            }));
    }

    return {
        mainText: 'CT Recomp',
        secondaryText: 'Native SNES recompilation',
        onclick: async () => {
            const items = await romItems();
            if (!items.length) {
                alert('No ROM found. Copy your own headerless dump into ' + DIR +
                      ' as rom.sfc, then try again.');
                return false;
            }
            showCarousel(items);
        },
    };
}
