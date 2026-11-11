# Simulator pluginu: posila aplikaci falesny stav Synergy, jako by bezelo OMSI.
# Pouziti: spust SynergyTablet.exe, pak  python tools/sim.py [main|turnus|menu|cinnost|sporic|manual]
# a otevri tablet UI v prohlizeci (adresa z okna aplikace, ?debug=1 ukaze hotspoty).
import socket,json,time,sys
mode=sys.argv[1] if len(sys.argv)>1 else "main"
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
stops=[("12:40","50","|B| Střížkov",""),("13:08","49","|B| Mělník,U Hřiště","x"),("13:12","48","|3| Mělník,Aut.nádraží",""),("13:13","47","|A| Mělník,Pivovar","x"),("13:14","46","|A| Mělník,Podolí",""),("13:21","45","|C| Liběchov","x"),("13:25","44","|A| Želízy","")]
v={"navigator_menu_visible":0,"klavesnice_vis":1,"occ_turnus_select":0,"occ_cil_select":0,"occ_sporic_menu_tl_vis":0,"occ_odpocet":1,
   "IBIS_odpocet":1,"ticketprinter_ticket_type_vis":1,"ticketprinter_ticket_kind_vis":1,"GivenTicket":-1,"occ_preselected_turnus_vis":-1,"occ_preselected_linkospoj_vis":-1}
st={"menu_texture":"..\\..\\SynergyOCC\\texture\\synergy_menu\\occ_main.png","occ_cas":"12:29:38","occ_datum":"06.05.26","occ_linkospoj":"100680/1001",
    "kasa_cena":"64,00 Kč","druh_listku":"Přestupní základní","zona_final":"<0|4,2230>","stops_min":"28 min","Kasa_od":"2:30","Kasa_od_text":"-","Kasa_od_units":"m","pocet_cestujicich_str":"12"}
for k,(t,n,name,x) in enumerate(stops):
    st["JKZ_Odchylka" if k==0 else f"JKZ_Odchylka{k}"]=t; st[f"stop{k}_cislo"]=n; st[f"Synergy_stop{k+1}"]=name; st[f"stop{k}_x"]=x
if mode=="turnus":
    v.update(navigator_menu_visible=30,occ_turnus_select=1,occ_preselected_turnus_vis=6)
    st["menu_texture"]="..\\..\\SynergyOCC\\texture\\synergy_menu\\occ_turnus.png"
    st["occ_turnusy_tab"]="@".join(["51.01  (06:10-19:30)","51.67  (07:20-19:30)","52.01  (05:20-15:46)","52.67  (08:20-20:32)","53.01  (05:05-19:46)","53.67  (14:10-00:39)","54.01  (13:10-00:39)","54.67  (06:10-11:41)","55.01  (04:35-18:46)","55.67  (08:10-21:39)","56.01  (10:10-22:39)","56.67  (05:10-15:41)","57.01  (03:50-17:46)","57.67  (07:40-20:09)","58.01  (04:35-17:47)","58.67  (10:35-17:17)","59.01  (06:05-21:12)"])
if mode=="menu":
    v.update(navigator_menu_visible=1)
    st["menu_texture"]="..\\..\\SynergyOCC\\texture\\synergy_menu\\occ_menu1_1.png"
    st.update(IBIS_turnus_str="",occ_linkospoj="",first_stop="",occ_odpocet_text="Otevření odpočtu",occ_cas="15:32:02",occ_datum="07.05.16")
if mode=="cinnost":
    v.update(navigator_menu_visible=57)
    st["menu_texture"]="..\\..\\SynergyOCC\\texture\\synergy_menu\\occ_cinnost.png"
    st["occ_cinnost_tab"]="přejezd@Střížkov@Mělník,Aut.nádraží@12:40 - 13:05@501400/1016@501400/1018"
if mode=="sporic":
    v.update(navigator_menu_visible=5,IBIS_odpocet=1,klavesnice_vis=0)
    st["menu_texture"]="..\\..\\SynergyOCC\\texture\\synergy_menu\\occ_sporic.png"
    st.update(IBIS_sporic_odchylka="-3:45 m",IBIS_sporic_datum="06. kvě",occ_cas="14:46:14",IBIS_sporic_stop_cas="14:50",IBIS_sporic_stop="Varnsdorf,Aut.nádr.",pocet_cestujicich_str="7")
if mode=="manual":
    v.update(navigator_menu_visible=34,occ_manual_select=0)
    st["menu_texture"]="..\\..\\SynergyOCC\\texture\\synergy_menu\\occ_manual.png"
    st.update(manual_linka_str="000000",manual_spoj_str="00",manual_cil_str="0-1")
print("posilam stav", mode, "- Ctrl+C konec")
while True:
    s.sendto(json.dumps({"v":v,"s":st},ensure_ascii=False).encode(),("127.0.0.1",38600))
    time.sleep(0.1)
