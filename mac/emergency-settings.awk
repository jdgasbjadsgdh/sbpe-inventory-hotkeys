# Enable the explicitly installed feature; preserve unrelated settings and keys.
function clean(v) { sub(/^[ \t]+/,"",v); sub(/[ \t\r]+$/,"",v); return v }
{
    lines[++count]=$0
    value=clean($0)
    if(value ~ /^\[/) {
        section=tolower(value);sub(/\].*/, "]",section);gsub(/[ \t\r]/,"",section)
        if(section=="[plugin_reconnect_lab]") lab_start=count
        if(section=="[sbpe_controls]") controls_start=count
    }
    if(value ~ /^[#;]/ || index(value,"=")==0) next
    at=index(value,"=");key=tolower(clean(substr(value,1,at-1)));setting=clean(substr(value,at+1))
    if(section=="[plugin_reconnect_lab]" && key=="enabled") { lines[count]="enabled = yes";has_enabled=1 }
    if(section=="[sbpe_controls]") {
        if(key=="disconnect_hold_primary") has_primary=1
        if(key=="disconnect_hold_secondary") has_secondary=1
        if(key=="disconnect_hold_tertiary") has_tertiary=1
    }
    if((section=="[sbpe_controls]" || section=="[plugin_keybinds]") && key !~ /^disconnect_hold_/ && tolower(setting)=="f4") busy=1
}
function bindings() {
    if(!has_primary) print "disconnect_hold_primary = " (busy ? "None" : "F4")
    if(!has_secondary) print "disconnect_hold_secondary = None"
    if(!has_tertiary) print "disconnect_hold_tertiary = None"
}
END {
    for(i=1;i<=count;i++) {
        print lines[i]
        if(i==lab_start && !has_enabled) print "enabled = yes"
        if(i==controls_start) bindings()
    }
    if(!lab_start) { print "\n[plugin_reconnect_lab]";print "enabled = yes" }
    if(!controls_start) { print "\n[sbpe_controls]";bindings() }
}
