import sys
VARS = ["navigator_menu_visible","klavesnice_vis","occ_turnus_select","occ_cil_select","occ_sporic_menu_tl_vis","occ_odpocet","occ_preselected_turnus_vis","occ_preselected_linkospoj_vis","IBIS_odpocet","ticketprinter_ticket_type_vis","ticketprinter_ticket_kind_vis","GivenTicket","occ_manual_select"] + [f"occ_linkospoj_row{i}" for i in range(17)]
STR = ["menu_texture","occ_cas","occ_datum","occ_linkospoj","IBIS_turnus_str","IBIS_turnus_str_full","first_stop",
 *[f"Synergy_stop{i}" for i in range(1,8)], "JKZ_Odchylka", *[f"JKZ_Odchylka{i}" for i in range(1,7)], *[f"stop{i}_cislo" for i in range(7)],
 "kasa_cena","druh_listku","Kasa_od","Kasa_od_text","Kasa_od_units","zona_final","stops_min",
 "occ_turnusy_tab","occ_linkospoj_tab","occ_sluzebni_cile_tab","kasa_id","kasa_pin",
 "manual_linka_str","manual_spoj_str","manual_cil_str","occ_cinnost_tab","occ_odpocet_text",
 "IBIS_sporic_stop","IBIS_sporic_stop_cas","IBIS_sporic_odchylka","IBIS_sporic_datum",
 "pocet_cestujicich_str","pocet_cestujicich_exit_str","occ_print_text", *[f"stop{i}_x" for i in range(7)]]
T = ["synergy_enter","ticketprinter_getticket","synergy_tl_menu","synergy_tl_esc","synergy_tl_vpred","synergy_tl_zpet","sipka_nahoru","sipka_dolu","sipka_nahoru2","sipka_dolu2",
 "synergy_tl_f1","synergy_tl_f4","synergy_tl_f5",
 *[f"displej_tl_{i}" for i in range(10)], "displej_tl_enter","displej_tl_esc","displej_tl_backspace",
 "occ_menu_jizda","occ_menu_zarizeni","occ_menu_is","occ_menu_nastaveni",
 "displej_tl_turnus","displej_tl_linkospoj","occ_odpocet_on_off","IBIS_logout","uzavreni_odpoctu",
 "occ_vypnuti","displej_tl_sporic","occ_f_klavesy", *[f"occ_sporic_mode{i}" for i in range(11)],
 "occ_sluzebni_zpravy","occ_sluzebni_cile1","occ_sluzebni_cile2","occ_smazani","displej_prepinani_tl","set_aut_hlaseni_on","set_aut_hlaseni_off",
 "select_linka","select_spoj","select_cil",
 *[f"select_turnus{i}" for i in range(17)], *[f"select_spoj{i}" for i in range(17)], *[f"select_cil{i}" for i in range(17)],
 "displej_predchozi_cinnost","displej_nasledujici_cinnost","IBIS_sporic_off","IBIS_login_pin",
 "ticketprinter_button_ticket_0","ticketprinter_button_ticket_1","ticketprinter_button_ticket_2","ticketprinter_button_ticket_3","ticketprinter_button_ticket_4",
 "ticketprinter_button_dummy_0","ticketprinter_button_dummy_1","ticketprinter_button_karta","ticketprinter_button_karta_ep","ticketprinter_button_karta_pk"]
T += ["cashdesk_changer_0_05","cashdesk_changer_0_10","cashdesk_changer_0_50","cashdesk_changer_1_00","cashdesk_changer_2_00","cashdesk_changer_5_00","cashdesk_changer_8_00"]
T += [t+"_off" for t in T if t.startswith("ticketprinter_button") or t.startswith("cashdesk_changer")]
# Pouziti: python3 gen.py [slozka_se_seznamy]
# Volitelna kontrola proti seznamum triggeru a promennych vytazenym ze skriptu Synergy
# (alltrig.txt, allv.txt). Ty nejsou soucasti repa - maji je jen ti, kdo maji Synergy.
import os
here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else None
if src:
    known = open(src+"/alltrig.txt").read().split()
    allv = open(src+"/allv.txt").read().split()
    for t in T: assert t in known, "trigger? "+t
    for v in VARS+STR:
        if v not in allv: print("pozor, neni ve varlistech Synergy:", v)
out = os.path.dirname(here)
with open(out+"/plugin/names.h","w") as f:
    q = lambda L: ",\n".join(f'    "{x}"' for x in L)
    f.write(f"static const char* VARS[] = {{\n{q(VARS)}\n}};\nstatic const char* STRVARS[] = {{\n{q(STR)}\n}};\nstatic const char* TRIGGERS[] = {{\n{q(T)}\n}};\n")
with open(out+"/plugin/SynergyBridge.opl","w",newline="\r\n") as f:
    f.write("[dll]\nSynergyBridge.dll\n\n")
    f.write(f"[varlist]\n{len(VARS)}\n" + "\n".join(VARS) + "\n\n")
    f.write(f"[stringvarlist]\n{len(STR)}\n" + "\n".join(STR) + "\n\n")
    f.write(f"[triggers]\n{len(T)}\n" + "\n".join(T) + "\n")
print(len(VARS), len(STR), len(T))
