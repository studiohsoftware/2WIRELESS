//This script retrieves raw preset data from the module and processes it.
//The processed data is then displayed on the page. The processed data is
//in the required JSON format for writing back to the module. (See SetPresets.html)
//
//Details:
//The raw data has memory addresses embedded within it, and these addresses must be found
//and separated from the preset data to produce the processed data.
//Each module is different, and the addresses are located using the dataskip[] values below.
//The dataskip values were found by manual inspection of the raw data!

function requestOnTimeout(e) {
    // XMLHttpRequest timed out.
    var textValue = result.innerHTML;
    textValue = textValue + "Request timed out.<br/>"
    result.innerHTML = textValue;
}

function requestOnLoad() {
    var status = request.status; // HTTP response status, e.g., 200 for "200 OK"
    var data = request.responseText; // Returned data, e.g., an HTML document.
    //result.innerHTML = data;
    var rawJsonMessage = JSON.parse(data);

    result.innerHTML = data + "<br/>WARNING: This module has an unknown data format.";


    var refinedJson;
    var presetData = [];
    var dataskip = [];
    var i;

    if (module_name.indexOf("206e") == 0) {
        //206e is mostly 40.
        for (i = 0; i < 38; i++) {
            dataskip[i] = 40;
        }
        dataskip[3] = 8;
        dataskip[4] = 32;
        dataskip[7] = 16;
        dataskip[8] = 24;
        dataskip[11] = 24;
        dataskip[12] = 16;
        dataskip[15] = 32;
        dataskip[16] = 8;
        dataskip[23] = 8;
        dataskip[24] = 32;
        dataskip[27] = 16;
        dataskip[28] = 24;
        dataskip[31] = 24;
        dataskip[32] = 16;
        dataskip[35] = 32;
        dataskip[36] = 8;
    }

    else if (module_name.indexOf("210e") == 0) {
        //210e is mostly 128.
        for (i = 0; i < 60; i++) {
            dataskip[i] = 128;
        }
        dataskip[1] = 32;
        dataskip[2] = 96;
        dataskip[3] = 64;
        dataskip[4] = 64;
        dataskip[5] = 96;
        dataskip[6] = 32;
        dataskip[9] = 32;
        dataskip[10] = 96;
        dataskip[11] = 64;
        dataskip[12] = 64;
        dataskip[13] = 96;
        dataskip[14] = 32;
        dataskip[17] = 32;
        dataskip[18] = 96;
        dataskip[19] = 64;
        dataskip[20] = 64;
        dataskip[21] = 96;
        dataskip[22] = 32;
        dataskip[25] = 32;
        dataskip[26] = 96;
        dataskip[27] = 64;
        dataskip[28] = 64;
        dataskip[29] = 96;
        dataskip[30] = 32;
        dataskip[33] = 32;
        dataskip[34] = 96;
        dataskip[35] = 64;
        dataskip[36] = 64;
        dataskip[37] = 96;
        dataskip[38] = 32;
        dataskip[41] = 32;
        dataskip[42] = 96;
        dataskip[43] = 64;
        dataskip[44] = 64;
        dataskip[45] = 96;
        dataskip[46] = 32;
        dataskip[49] = 32;
        dataskip[50] = 96;
        dataskip[51] = 64;
        dataskip[52] = 64;
        dataskip[53] = 96;
        dataskip[54] = 32;
        dataskip[57] = 32;
        dataskip[58] = 96;
        dataskip[59] = 64;
    }

    else if (module_name.indexOf("223e A") == 0) {
        //223e A is mostly 128.
        for (i = 0; i < 73; i++) {
            dataskip[i] = 128;
        }
        dataskip[1] = 58;
        dataskip[2] = 70;
        dataskip[3] = 116;
        dataskip[4] = 12;
        dataskip[6] = 46;
        dataskip[7] = 82;
        dataskip[8] = 104;
        dataskip[9] = 24;
        dataskip[11] = 34;
        dataskip[12] = 94;
        dataskip[13] = 92;
        dataskip[14] = 36;
        dataskip[16] = 22;
        dataskip[17] = 106;
        dataskip[18] = 80;
        dataskip[19] = 48;
        dataskip[21] = 10;
        dataskip[22] = 118;
        dataskip[23] = 68;
        dataskip[24] = 60;
        dataskip[25] = 126;
        dataskip[26] = 2;
        dataskip[28] = 56;
        dataskip[29] = 72;
        dataskip[30] = 114;
        dataskip[31] = 14;
        dataskip[33] = 44;
        dataskip[34] = 84;
        dataskip[35] = 102;
        dataskip[36] = 26;
        dataskip[38] = 32;
        dataskip[39] = 96;
        dataskip[40] = 90;
        dataskip[41] = 38;
        dataskip[43] = 20;
        dataskip[44] = 108;
        dataskip[45] = 78;
        dataskip[46] = 50;
        dataskip[48] = 8;
        dataskip[49] = 120;
        dataskip[50] = 66;
        dataskip[51] = 62;
        dataskip[52] = 124;
        dataskip[53] = 32;
        dataskip[54] = 100;
        dataskip[55] = 54;
        dataskip[56] = 74;
        dataskip[57] = 112;
        dataskip[58] = 16;
        dataskip[60] = 42;
        dataskip[61] = 86;
        dataskip[62] = 100;
        dataskip[63] = 28;
        dataskip[65] = 30;
        dataskip[66] = 98;
        dataskip[67] = 88;
        dataskip[68] = 40;
        dataskip[70] = 18;
        dataskip[71] = 110;
        dataskip[72] = 76;
    }

    else if (module_name.indexOf("223e M") == 0) {
        //223e M is mostly 128.
        for (i = 0; i < 315; i++) {
            dataskip[i] = 128;
        }
        dataskip[9] = 96;
        dataskip[10] = 32;
        dataskip[20] = 64;
        dataskip[21] = 64;
        dataskip[31] = 32;
        dataskip[32] = 96;
        dataskip[51] = 96;
        dataskip[52] = 32;
        dataskip[62] = 64;
        dataskip[63] = 64;
        dataskip[73] = 32;
        dataskip[74] = 96;
        dataskip[93] = 96;
        dataskip[94] = 32;
        dataskip[104] = 64;
        dataskip[105] = 64;
        dataskip[115] = 32;
        dataskip[116] = 96;
        dataskip[135] = 96;
        dataskip[136] = 32;
        dataskip[146] = 64;
        dataskip[147] = 64;
        dataskip[157] = 32;
        dataskip[158] = 96;
        dataskip[177] = 96;
        dataskip[178] = 32;
        dataskip[188] = 64;
        dataskip[189] = 64;
        dataskip[199] = 32;
        dataskip[200] = 96;
        dataskip[219] = 96;
        dataskip[220] = 32;
        dataskip[230] = 64;
        dataskip[231] = 64;
        dataskip[241] = 32;
        dataskip[242] = 96;
        dataskip[261] = 96;
        dataskip[262] = 32;
        dataskip[272] = 64;
        dataskip[273] = 64;
        dataskip[283] = 32;
        dataskip[284] = 96;
        dataskip[303] = 96;
        dataskip[304] = 32;
        dataskip[314] = 64;
    }

    else if (module_name.indexOf("225e") == 0) {
        //225e every line is different.
        dataskip[0] = 128;
        dataskip[1] = 12;
        dataskip[2] = 116;
        dataskip[3] = 24;
        dataskip[4] = 104;
        dataskip[5] = 36;
        dataskip[6] = 92;
        dataskip[7] = 48;
        dataskip[8] = 80;
        dataskip[9] = 60;
        dataskip[10] = 68;
        dataskip[11] = 72;
        dataskip[12] = 56;
        dataskip[13] = 84;
        dataskip[14] = 44;
        dataskip[15] = 96;
        dataskip[16] = 32;
        dataskip[17] = 108;
        dataskip[18] = 20;
        dataskip[19] = 120;
        dataskip[20] = 8;
        dataskip[21] = 128;
        dataskip[22] = 4;
        dataskip[23] = 124;
        dataskip[24] = 16;
        dataskip[25] = 112;
        dataskip[26] = 28;
        dataskip[27] = 100;
        dataskip[28] = 40;
        dataskip[29] = 88;
        dataskip[30] = 52;
        dataskip[31] = 76;
        dataskip[32] = 64;
        dataskip[33] = 64;
        dataskip[34] = 76;
        dataskip[35] = 52;
        dataskip[36] = 88;
        dataskip[37] = 40;
        dataskip[38] = 100;
        dataskip[39] = 28;
        dataskip[40] = 112;
        dataskip[41] = 16;
        dataskip[42] = 124;
        dataskip[43] = 4;
        dataskip[44] = 128;
        dataskip[45] = 8;
        dataskip[46] = 120;
        dataskip[47] = 20;
        dataskip[48] = 108;
        dataskip[49] = 32;
        dataskip[50] = 96;
        dataskip[51] = 44;
        dataskip[52] = 84;
        dataskip[53] = 56;
        dataskip[54] = 72;
        dataskip[55] = 68;
        dataskip[56] = 60;
        dataskip[57] = 80;
        dataskip[58] = 48;
        dataskip[59] = 92;
        dataskip[60] = 36;
        dataskip[61] = 104;
    }

    else if (module_name.indexOf("227e") == 0) {
        //227e is mostly 28.
        for (i = 0; i < 36; i++) {
            dataskip[i] = 28;
        }
        dataskip[4] = 16;
        dataskip[5] = 12;
        dataskip[10] = 4;
        dataskip[11] = 24;
        dataskip[15] = 20;
        dataskip[16] = 8;
        dataskip[21] = 8;
        dataskip[22] = 20;
        dataskip[26] = 24;
        dataskip[27] = 4;
        dataskip[32] = 12;
        dataskip[33] = 16;
    }

    else if (module_name.indexOf("250e") == 0) {
        //250e is mostly 128.
        for (i = 0; i < 158; i++) {
            dataskip[i] = 128;
        }
        dataskip[4] = 48;
        dataskip[5] = 80;
        dataskip[9] = 96;
        dataskip[10] = 32;
        dataskip[15] = 16;
        dataskip[16] = 112;
        dataskip[20] = 64;
        dataskip[21] = 64;
        dataskip[25] = 112;
        dataskip[26] = 16;
        dataskip[31] = 32;
        dataskip[32] = 96;
        dataskip[36] = 80;
        dataskip[37] = 48;
        dataskip[46] = 48;
        dataskip[47] = 80;
        dataskip[51] = 96;
        dataskip[52] = 32;
        dataskip[57] = 16;
        dataskip[58] = 112;
        dataskip[62] = 64;
        dataskip[63] = 64;
        dataskip[67] = 112;
        dataskip[68] = 16;
        dataskip[73] = 32;
        dataskip[74] = 96;
        dataskip[78] = 80;
        dataskip[79] = 48;
        dataskip[88] = 48;
        dataskip[89] = 80;
        dataskip[93] = 96;
        dataskip[94] = 32;
        dataskip[99] = 16;
        dataskip[100] = 112;
        dataskip[104] = 64;
        dataskip[105] = 64;
        dataskip[109] = 112;
        dataskip[110] = 16
        dataskip[115] = 32;
        dataskip[116] = 96;
        dataskip[120] = 80;
        dataskip[121] = 48
        dataskip[130] = 48;
        dataskip[131] = 80;
        dataskip[135] = 96;
        dataskip[136] = 32;
        dataskip[141] = 16;
        dataskip[142] = 112;
        dataskip[146] = 64;
        dataskip[147] = 64;
        dataskip[151] = 112;
        dataskip[152] = 16;
        dataskip[157] = 32;

    }

    else if (module_name.indexOf("251e") == 0) {
        //251e has 128 characters with some exceptions.
        for (i = 0; i < 1013; i++) {
            dataskip[i] = 128;
        }
        dataskip[32] = 112;
        dataskip[33] = 16;
        dataskip[66] = 96;
        dataskip[67] = 32;
        dataskip[100] = 80;
        dataskip[101] = 48;
        dataskip[134] = 64;
        dataskip[135] = 64;
        dataskip[168] = 48;
        dataskip[169] = 80;
        dataskip[202] = 32;
        dataskip[203] = 96;
        dataskip[236] = 16;
        dataskip[237] = 112;
        dataskip[302] = 112;
        dataskip[303] = 16;
        dataskip[336] = 96;
        dataskip[337] = 32;
        dataskip[370] = 80;
        dataskip[371] = 48;
        dataskip[404] = 64;
        dataskip[405] = 64;
        dataskip[438] = 48;
        dataskip[439] = 80;
        dataskip[472] = 32;
        dataskip[473] = 96;
        dataskip[506] = 16;
        dataskip[507] = 112;
        dataskip[572] = 112;
        dataskip[573] = 16;
        dataskip[606] = 96;
        dataskip[607] = 32;
        dataskip[640] = 80;
        dataskip[641] = 48;
        dataskip[674] = 64;
        dataskip[675] = 64;
        dataskip[708] = 48;
        dataskip[709] = 80;
        dataskip[742] = 32;
        dataskip[743] = 96;
        dataskip[776] = 16;
        dataskip[777] = 112;
        dataskip[842] = 112;
        dataskip[843] = 16;
        dataskip[876] = 96;
        dataskip[877] = 32;
        dataskip[910] = 80;
        dataskip[911] = 48;
        dataskip[944] = 64;
        dataskip[945] = 64;
        dataskip[978] = 48;
        dataskip[979] = 80;
        dataskip[1012] = 32;
    }

    else if (module_name.indexOf("254e") > -1) {
        //254e is 32.
        for (i = 0; i < 30; i++) {
            dataskip[i] = 32;
        }
    }

    else if (module_name.indexOf("255e") > -1) {
        //255e is mostly 72.
        //Note 255e data is packed. Each value is 12 bits, so bytes are shared to save space.
        //Refer to CSR comments below. Same thing here except PRESET_LENGTH=24 and
        //PACKED_PRESET_LENGTH=18.
        for (i = 0; i < 38; i++) {
            dataskip[i] = 72;
        }
        dataskip[3] = 40;
        dataskip[4] = 32;
        dataskip[8] = 8;
        dataskip[9] = 64;
        dataskip[12] = 48;
        dataskip[13] = 24;
        dataskip[17] = 16;
        dataskip[18] = 56;
        dataskip[21] = 56;
        dataskip[22] = 16;
        dataskip[26] = 24;
        dataskip[27] = 48;
        dataskip[30] = 64;
        dataskip[31] = 8;
        dataskip[35] = 32;
        dataskip[36] = 40;
    }

    else if (module_name.indexOf("256e") == 0) {
        //256e is mostly 44.
        for (i = 0; i < 40; i++) {
            dataskip[i] = 44;
        }
        dataskip[2] = 40;
        dataskip[3] = 4;
        dataskip[6] = 36;
        dataskip[7] = 8;
        dataskip[10] = 32;
        dataskip[11] = 12;
        dataskip[14] = 28;
        dataskip[15] = 16;
        dataskip[18] = 24;
        dataskip[19] = 20;
        dataskip[22] = 20;
        dataskip[23] = 24;
        dataskip[26] = 16;
        dataskip[27] = 28;
        dataskip[30] = 12;
        dataskip[31] = 32;
        dataskip[34] = 8;
        dataskip[35] = 36;
        dataskip[38] = 4;
        dataskip[39] = 40;
    }

    else if (module_name.indexOf("259e") == 0) {
        //259e has a pattern. Every third line is 66.
        for (i = 0; i < 45; i++) {
            dataskip[i] = 66;
        }
        dataskip[1] = 62;
        dataskip[2] = 4;
        dataskip[4] = 58;
        dataskip[5] = 8;
        dataskip[7] = 54;
        dataskip[8] = 12;
        dataskip[10] = 52;
        dataskip[11] = 14;
        dataskip[13] = 46;
        dataskip[14] = 20;
        dataskip[16] = 42;
        dataskip[17] = 24;
        dataskip[19] = 38;
        dataskip[20] = 28;
        dataskip[22] = 34;
        dataskip[23] = 32;
        dataskip[25] = 30;
        dataskip[26] = 36;
        dataskip[28] = 26;
        dataskip[29] = 40;
        dataskip[31] = 22;
        dataskip[32] = 44;
        dataskip[34] = 18;
        dataskip[35] = 48;
        dataskip[37] = 14;
        dataskip[38] = 52;
        dataskip[40] = 10;
        dataskip[41] = 56;
        dataskip[43] = 6;
        dataskip[44] = 60;
    }

    else if (module_name.indexOf("261e") == 0) {
        //261e is mostly 62.
        for (i = 0; i < 44; i++) {
            dataskip[i] = 62;
        }
        dataskip[2] = 4;
        dataskip[3] = 58;
        dataskip[5] = 8;
        dataskip[6] = 54;
        dataskip[8] = 12;
        dataskip[9] = 50;
        dataskip[11] = 16;
        dataskip[12] = 46;
        dataskip[14] = 20;
        dataskip[15] = 42;
        dataskip[17] = 24;
        dataskip[18] = 38;
        dataskip[20] = 28;
        dataskip[21] = 34;
        dataskip[23] = 32;
        dataskip[24] = 30;
        dataskip[26] = 36;
        dataskip[27] = 26;
        dataskip[29] = 40;
        dataskip[30] = 22;
        dataskip[32] = 44;
        dataskip[33] = 18;
        dataskip[35] = 48;
        dataskip[36] = 14;
        dataskip[38] = 52;
        dataskip[39] = 10;
        dataskip[41] = 56;
        dataskip[42] = 6;
    }

    else if (module_name.indexOf("266e") == 0) {
        //266e is mostly 14.
        for (i = 0; i < 33; i++) {
            dataskip[i] = 14;
        }
        dataskip[9] = 2;
        dataskip[10] = 12;
        dataskip[19] = 4;
        dataskip[20] = 10;
        dataskip[29] = 6;
        dataskip[30] = 8;
    }

    else if (module_name.indexOf("281e") == 0) {
        //281e is mostly 14.
        for (i = 0; i < 33; i++) {
            dataskip[i] = 14;
        }
        dataskip[9] = 2;
        dataskip[10] = 12;
        dataskip[19] = 4;
        dataskip[20] = 10;
        dataskip[29] = 6;
        dataskip[30] = 8;
    }

    else if (module_name.indexOf("285e F") == 0) {
        //285e FM is mostly 14.
        for (i = 0; i < 33; i++) {
            dataskip[i] = 14;
        }
        dataskip[9] = 2;
        dataskip[10] = 12;
        dataskip[19] = 4;
        dataskip[20] = 10;
        dataskip[29] = 6;
        dataskip[30] = 8;
    }

    else if (module_name.indexOf("285e B") == 0) {
        //285e BM is mostly 22.
        for (i = 0; i < 35; i++) {
            dataskip[i] = 22;
        }
        dataskip[5] = 18;
        dataskip[6] = 4;
        dataskip[12] = 14;
        dataskip[13] = 8;
        dataskip[19] = 10;
        dataskip[20] = 12;
        dataskip[26] = 6;
        dataskip[27] = 16;
        dataskip[33] = 2;
        dataskip[34] = 20;
    }

    else if (module_name.indexOf("291e") == 0) {
        //291e has 128 characters with some exceptions.
        for (i = 0; i < 140; i++) {
            dataskip[i] = 128;
        }
        dataskip[3] = 86;
        dataskip[4] = 42;
        dataskip[8] = 44;
        dataskip[9] = 84;
        dataskip[13] = 2;
        dataskip[14] = 126;
        dataskip[17] = 88;
        dataskip[18] = 40;
        dataskip[22] = 46;
        dataskip[23] = 82;
        dataskip[27] = 4;
        dataskip[28] = 124;
        dataskip[31] = 90;
        dataskip[32] = 38;
        dataskip[36] = 48;
        dataskip[37] = 80;
        dataskip[41] = 6;
        dataskip[42] = 122;
        dataskip[45] = 92;
        dataskip[46] = 36;
        dataskip[50] = 50;
        dataskip[51] = 78;
        dataskip[55] = 8;
        dataskip[56] = 120;
        dataskip[59] = 94;
        dataskip[60] = 34;
        dataskip[64] = 52;
        dataskip[65] = 76;
        dataskip[69] = 10;
        dataskip[70] = 118;
        dataskip[73] = 96;
        dataskip[74] = 32;
        dataskip[78] = 54;
        dataskip[79] = 74;
        dataskip[83] = 12;
        dataskip[84] = 116;
        dataskip[87] = 98;
        dataskip[88] = 30;
        dataskip[92] = 56;
        dataskip[93] = 72;
        dataskip[97] = 14;
        dataskip[98] = 114;
        dataskip[101] = 100;
        dataskip[102] = 28;
        dataskip[106] = 58;
        dataskip[107] = 70;
        dataskip[111] = 16;
        dataskip[112] = 112;
        dataskip[115] = 102;
        dataskip[116] = 26;
        dataskip[120] = 60;
        dataskip[121] = 68;
        dataskip[125] = 18;
        dataskip[126] = 110;
        dataskip[129] = 104;
        dataskip[130] = 24;
        dataskip[134] = 62;
        dataskip[135] = 66;
        dataskip[139] = 20;
    }

    else if (module_name.indexOf("292e") == 0) {
        //292e is easy. First four characters are address, then 16 characters of data,
        //then four more characters of address, another 16 and so on until the end.
        for (i = 0; i < 30; i++) {
            dataskip[i] = 16;
        }
    }

    else if (module_name.indexOf("296e") == 0) {
        //296e is half 128. Other half is 64.
        for (i = 0; i < 60; i++) {
            dataskip[i] = 128;
        }
        dataskip[1] = 64;
        dataskip[2] = 64;
        dataskip[5] = 64;
        dataskip[6] = 64;
        dataskip[9] = 64;
        dataskip[10] = 64;
        dataskip[13] = 64;
        dataskip[14] = 64;
        dataskip[17] = 64;
        dataskip[18] = 64;
        dataskip[21] = 64;
        dataskip[22] = 64;
        dataskip[25] = 64;
        dataskip[26] = 64;
        dataskip[29] = 64;
        dataskip[30] = 64;
        dataskip[33] = 64;
        dataskip[34] = 64;
        dataskip[37] = 64;
        dataskip[38] = 64;
        dataskip[41] = 64;
        dataskip[42] = 64;
        dataskip[45] = 64;
        dataskip[46] = 64;
        dataskip[49] = 64;
        dataskip[50] = 64;
        dataskip[53] = 64;
        dataskip[54] = 64;
        dataskip[57] = 64;
        dataskip[58] = 64;
    }

    else if (module_name.indexOf("Studio H CSR") == 0) {
        //Every line different in CSR.

        //Note CSR data is packed. Each value is 12 bits, so bytes are shared to save space.
        //Unpacking will be necessary if you want to edit the data. Packing would be necessary
        //before writing the edited data back to the module.
        //If no editing is needed, no need to unpack and pack. Just leave the data as received
        //and send it back as it is.

        /* HERE IS THE PACK CODE USED BY THE MODULE.
           On the CSR, PRESET_LENGTH=70. PACKED_PRESET_LENGTH is 53.
          //Pack data into packed_data.
          //First get the nibbles into a byte array as the lower nibble in each byte
          int j = 0;
          for (int i=0;i<PRESET_LENGTH;i++){
            tmp_data[j] = 0 | ((data[i] & 0x000F));
            j++;
            tmp_data[j] = 0 | ((data[i] & 0x00F0) >> 4);
            j++;
            tmp_data[j] = 0 | ((data[i] & 0x0F00) >> 8);
            j++;
          }

          //Now read all of the nibbles into the packed array
          j = 0;
          for (int i=0;i<PACKED_PRESET_LENGTH;i++){
            packed_data[i] = tmp_data[j] & 0x000F;
            j++;
            packed_data[i] = packed_data[i] | ((tmp_data[j] & 0x000F) << 4);
            j++;
            packed_data[i] = packed_data[i] | ((tmp_data[j] & 0x000F) << 8);
            j++;
            packed_data[i] = packed_data[i] | ((tmp_data[j] & 0x000F) << 12);
            j++;
          }
         */

        /* HERE IS THE UNPACK CODE USED BY THE MODULE.
           On the CSR, PRESET_LENGTH=70. PACKED_PRESET_LENGTH is 53.
          //Unpack packed_data into data.
          //First read the packed data nibbles into bytes
          int j = 0;
          for (int i=0;i<PACKED_PRESET_LENGTH;i++){
            tmp_data[j] = 0  | (packed_data[i] & 0x000F);
            j++;
            tmp_data[j] = 0  | ((packed_data[i] & 0x00F0) >> 4);
            j++;
            tmp_data[j] = 0  | ((packed_data[i] & 0x0F00) >> 8);
            j++;
            tmp_data[j] = 0  | ((packed_data[i] & 0xF000) >> 12);
            j++;
          }

          //Now read the nibbles into 12 bit words
          j = 0;
          for (int i=0;i<PRESET_LENGTH;i++){
            data[i] = 0 | tmp_data[j];
            j++;
            data[i] = data[i] | (tmp_data[j] << 4);
            j++;
            data[i] = data[i] | (tmp_data[j] << 8);
            j++;
          }
        */
        dataskip[0] = 212;
        dataskip[1] = 44;
        dataskip[2] = 168;
        dataskip[3] = 88;
        dataskip[4] = 124;
        dataskip[5] = 132;
        dataskip[6] = 80;
        dataskip[7] = 176;
        dataskip[8] = 36;
        dataskip[9] = 212;
        dataskip[10] = 8;
        dataskip[11] = 204;
        dataskip[12] = 52;
        dataskip[13] = 160;
        dataskip[14] = 96;
        dataskip[15] = 116;
        dataskip[16] = 140;
        dataskip[17] = 72;
        dataskip[18] = 184;
        dataskip[19] = 28;
        dataskip[20] = 212;
        dataskip[21] = 16;
        dataskip[22] = 196;
        dataskip[23] = 60;
        dataskip[24] = 152;
        dataskip[25] = 104;
        dataskip[26] = 108;
        dataskip[27] = 148;
        dataskip[28] = 64;
        dataskip[29] = 192;
        dataskip[30] = 20;
        dataskip[31] = 212;
        dataskip[32] = 24;
        dataskip[33] = 188;
        dataskip[34] = 68;
        dataskip[35] = 144;
        dataskip[36] = 112;
        dataskip[37] = 100;
        dataskip[38] = 156;
        dataskip[39] = 56;
        dataskip[40] = 200;
        dataskip[41] = 12;
        dataskip[42] = 212;
        dataskip[43] = 32;
        dataskip[44] = 180;
        dataskip[45] = 76;
        dataskip[46] = 136;
        dataskip[47] = 120;
        dataskip[48] = 92;
        dataskip[49] = 164;
        dataskip[50] = 48;
        dataskip[51] = 208;
        dataskip[52] = 4;
        dataskip[53] = 212;
    }

    else if (module_name.indexOf("Studio H DPO") == 0) {
        //DPO is 64 per line.
        for (i = 0; i < 30; i++) {
            dataskip[i] = 64;
        }
    }

    var rawdata = rawJsonMessage.data;
    for (i = 0; i < dataskip.length; i++) {
        var memory_address = rawdata.substring(0, 4);
        rawdata = rawdata.substring(4);
        var preset_data = rawdata.substring(0, dataskip[i]);
        rawdata = rawdata.substring(dataskip[i]);
        var presetDataElement = {
            addr: memory_address,
            data: preset_data
        }
        presetData.push(presetDataElement);
    }


    if (presetData.length > 0) {
        var refinedJson = {
            module_address: module_address,
            module_name: module_name,
            presets: presetData
        }
        var refinedJsonString = JSON.stringify(refinedJson);
        result.innerHTML = refinedJsonString;
        save_div.visibility = "visible";
        document.getElementById("saveLink").download = module_name + ".json";
        document.getElementById("saveLink").href = "data:application/xml;charset=utf-8," + refinedJsonString;
    }
}
