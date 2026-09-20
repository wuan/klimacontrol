#ifndef KLIMACONTROL_VERSION_COMPARE_H
#define KLIMACONTROL_VERSION_COMPARE_H

#include <cstdio>

namespace Support {

    /**
     * Compare two firmware version tags of the form `vMAJOR.MINOR.PATCH`.
     *
     * Any trailing suffix is ignored, which is what makes untagged developer
     * builds behave correctly: `scripts/get_version.py` falls back to
     * `git describe --tags --always`, so a build four commits past v1.2.3
     * reports `v1.2.3-4-gabc1234`. Compared numerically that is *equal* to
     * v1.2.3, so the device does not offer itself the v1.2.3 release as an
     * "update" (which would in fact be a downgrade). A real new release
     * (v1.3.0) still compares greater and is offered normally.
     *
     * A version that does not parse yields 0 ("not newer"), so the caller
     * refuses the update rather than flashing something it cannot reason
     * about.
     *
     * @return  1 if `available` is strictly newer than `current`
     *         -1 if `available` is strictly older than `current`
     *          0 if they are equal, or either version is unparseable
     */
    inline int compareVersions(const char *current, const char *available) {
        if (current == nullptr || available == nullptr) {
            return 0;
        }

        int cv[3] = {0, 0, 0};
        int av[3] = {0, 0, 0};

        if (sscanf(current, "v%d.%d.%d", &cv[0], &cv[1], &cv[2]) != 3) {
            return 0;
        }
        if (sscanf(available, "v%d.%d.%d", &av[0], &av[1], &av[2]) != 3) {
            return 0;
        }

        for (int i = 0; i < 3; i++) {
            if (av[i] > cv[i]) return 1;
            if (av[i] < cv[i]) return -1;
        }
        return 0;
    }

    /**
     * True if `available` is a strictly newer release than `current`.
     * This is the predicate that gates both the "update available" flag in the
     * API and the decision to actually flash, so a downgrade or a re-flash of
     * the running version is never offered or performed.
     */
    inline bool isNewerVersion(const char *current, const char *available) {
        return compareVersions(current, available) > 0;
    }

    /**
     * Gate behind both the default strict-newer update path and the opt-in
     * reinstall path on POST /api/ota/update. Strictly newer always proceeds;
     * strictly older never proceeds (the existing "Older release is never
     * installed" scenario); semver-equal proceeds only when the caller has set
     * `allowReinstall`. Lives in this header so the native test can exercise it
     * without dragging in OTA headers.
     */
    inline bool isReinstallOrNewer(const char *current,
                                   const char *available,
                                   bool allowReinstall) {
        int cmp = compareVersions(current, available);
        if (cmp > 0) return true;            // strictly newer
        if (cmp < 0) return false;           // strictly older — refused either way
        return allowReinstall;               // semver-equal — gated by flag
    }

} // namespace Support

#endif // KLIMACONTROL_VERSION_COMPARE_H
