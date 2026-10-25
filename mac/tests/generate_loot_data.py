"""Regenerate native lookup tables from the supplied Windows gameplay data."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
quote = json.dumps

def choices(value):
    return value if isinstance(value, list) else [value or ""]

lines = ["/* Generated from data/items.json and data/boosts.json. */",
         "struct LootName {int id;const char *name,*kind,*slot;};",
         "static const struct LootName loot_names[]={"]
for item in json.loads((root / "data/items.json").read_text()):
    lines.append("{%d,%s,%s,%s}," % (
        item["id"], quote(item["name"]),
        quote(" ".join(choices(item.get("class")))),
        quote(" ".join(choices(item.get("slot"))))))
lines += ["};", "struct BoostName {int id;const char *name,*kind,*slot;int display,divide;};",
          "static const struct BoostName boost_names[]={"]
for boost in json.loads((root / "data/boosts.json").read_text()):
    for kind in choices(boost.get("class")):
        for slot in choices(boost.get("slot")):
            lines.append("{%d,%s,%s,%s,%d,%d}," % (
                boost["id"], quote(boost["name"]), quote(kind), quote(slot),
                boost.get("display_type", 0), boost.get("divide_value", 0)))
lines += ["};"]
rules = json.loads((root / "data/chest-rules.json").read_text())
lines += ["struct ChestRule {const char *name,*variant;int wildcard,count;const char *mods[8];};"]
for key, table in [("remove_filter_list", "chest_remove_rules"),
                   ("display_filter_list", "chest_display_rules"),
                   ("perf_tracking", "chest_perf_rules")]:
    lines.append("static const struct ChestRule %s[]={" % table)
    for rule in rules[key]:
        if key == "perf_tracking" and not rule.get("tracking"):
            continue
        mods = rule.get("modifiers", [])
        if len(mods) > 8:
            raise ValueError("Chest rules support up to eight modifiers")
        lines.append("{%s,%s,%d,%d,{%s}}," % (
            quote(rule["name"]), quote(rule.get("variant", "")),
            int("modifiers" not in rule), len(mods),
            ",".join(map(quote, mods)) or "NULL"))
    lines += ["{NULL,NULL,0,0,{NULL}}", "};"]
(root / "src/loot_data.h").write_text("\n".join(lines) + "\n")
