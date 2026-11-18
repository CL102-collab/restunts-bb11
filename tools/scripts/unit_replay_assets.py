"""Stage the McMurtry resources used by the UNIT replay regressions."""

import shutil


MCMU_FILES = ("CARMCMU.RES", "STMCMU.3SH", "STDAMCMU.VSH", "STDBMCMU.VSH")


def stage_mcmu_game(data, assets, game):
    shutil.copytree(data.resolve(), game)
    for name in MCMU_FILES:
        destination = game / name
        destination.unlink(missing_ok=True)
        shutil.copyfile(assets / name, destination)
