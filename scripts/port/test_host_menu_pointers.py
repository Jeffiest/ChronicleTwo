#!/usr/bin/env python3
"""Regression checks for the generated menu option pointer-width rewrite."""

import pathlib
import sys

from host_menu_pointers import fixture_source, transform


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ps2" / "src" / "menuop.cpp"


def check_transform():
    original = SOURCE.read_text(encoding="latin-1")
    updated = transform("src/menuop.cpp", original)
    assert "int                  offset;" not in updated
    assert "for (i = 0; i < OPTION_BUTTON_NUM; i++) {" in updated
    assert "EnableButton(loop_row[i]);" in updated
    assert "(u8 *) loop_row + offset" not in updated
    assert "OptionButtonForm != NULL" in updated
    assert "DC_MENU_OPTION_TEST" not in updated

    fixture = fixture_source(updated)
    assert fixture.count("void CMenuOption::DefaultButton(") == 1
    assert fixture.count("void CMenuOption::EnableButton(") == 1
    assert fixture.count("void CMenuOption::UpdateOptionForm(") == 1
    assert "OptionButtonForm != NULL" in fixture
    assert "OptionButtonForm = enabled ? reinterpret_cast<CMenuPosDataForm *>(1) : NULL;" in fixture

    try:
        fixture_source(original)
    except ValueError:
        pass
    else:
        raise AssertionError("fixture extraction accepted an untransformed source")
    try:
        transform("src/menuop.cpp", updated)
    except ValueError:
        pass
    else:
        raise AssertionError("a second production transform was not rejected")

    for bad_key in ("src/menuop2.cpp", "menuop.cpp"):
        try:
            transform(bad_key, original)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unexpectedly accepted rewrite key {bad_key}")

    changed_site = original.replace(
        "EnableButton(*(MENUFORMPARTS_TYPE **) ((u8 *) loop_row + offset));",
        "EnableButton(*(MENUFORMPARTS_TYPE **) ((u8 *) loop_row + offset + 1));")
    try:
        transform("src/menuop.cpp", changed_site)
    except ValueError:
        pass
    else:
        raise AssertionError("source drift at the pointer expression did not fail")


if __name__ == "__main__":
    try:
        check_transform()
    except AssertionError as error:
        print(f"menu pointer regression failed: {error}", file=sys.stderr)
        raise
    print("menu pointer regression passed")
