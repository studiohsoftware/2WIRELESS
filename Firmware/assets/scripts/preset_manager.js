function sBOnClick() {
    send("savepreset?preset=" + r_pset.value);
    names[r_pset.value] = names[c_pset.value];
    writeName(r_pset.value, names[r_pset.value]);
    rOnChange();
}

function rBOnClick() {
    if (c_pset.value != r_pset.value) {
        c_pset.value = r_pset.value;
        cOnChange();
    }
}

function lBOnClick() {
    var pset = c_pset.value;
    pset = pset - 1;
    if (pset < 1) {
        pset = pset + 30;
    }
    pset = pset % 31;
    c_pset.value = pset;
    r_pset.value = pset;
    cOnChange();
    rOnChange();
}
function nBOnClick() {
    var pset = c_pset.value;
    pset = pset % 30 + 1;
    c_pset.value = pset;
    r_pset.value = pset;
    cOnChange();
    rOnChange();
}
function remBOnClick() {
    rem = !rem;
    if (rem) {
        send("remoteenable");
    } else {
        send("remotedisable");
    }
}

function cOnChange() {
    c_name.value = names[c_pset.value];
    send("recallpreset?preset=" + c_pset.value);
    writePreset(c_pset.value);
}

function rOnChange() {
    r_name.value = names[r_pset.value];
}

function cnameOnChange() {
    names[c_pset.value] = c_name.value;
    writeName(c_pset.value, names[c_pset.value]);
    rOnChange();
}

function send(url) {
    var version = "";
    if (document.getElementById('v2').checked) {
        version = "v2/";
    }
    req = new XMLHttpRequest();
    req.open("GET", "http://192.168.0.1/" + version + url, false);
    req.send(null);
}

function writePreset(pset) {
    var pStr = pset.toString(16);
    if (pStr.length == 1) pStr = "0" + pStr;
    send("writememory?addr=" + (baseAddr + 6).toString(16) + "&data=" + pStr);
}

function readPreset() {
    send("readmemory?addr=" + (baseAddr + 6).toString(16) + "&length=1");
    return (parseInt(JSON.parse(req.responseText)[0].data));
}

function readName(pset) {
    send("readmemory?addr=" + (baseAddr + 7 + ((pset - 1) * 21)).toString(16) + "&length=21");
    return hexToText(JSON.parse(req.responseText)[0].data);
}

function writeName(pset, name) {
    var addr = baseAddr + 7 + ((pset - 1) * 21);
    send("writememory?addr=" + addr.toString(16) + "&data=" + textToHex(name));
}

function hexToText(hex) {
    var result = "";
    if (hex != "") {
        var code = parseInt(hex.substring(0, 2), 16);
        if (code == 0) {
            return result;
        }
        else if (code > 19) {
            result = String.fromCharCode(code) + hexToText(hex.substring(2, hex.length));
        }
    }
    return result;
}

function textToHex(text) {
    var result = "";
    if (text != "") {
        var i;
        for (i = 0; i < text.length; i++) {
            result = result + text[i].charCodeAt(0).toString(16);
        }
    }
    return result + "00";
}
