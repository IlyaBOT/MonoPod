from pathlib import Path
import json,csv,re,math,collections,xml.etree.ElementTree as ET
import pcbnew as p
from design import ROOT,NAME,COMP
b=p.LoadBoard(str(ROOT/f'{NAME}.kicad_pcb'));b.BuildConnectivity()
fps={f.GetReference():f for f in b.GetFootprints()}
assert set(fps)==set(COMP)
expected=json.loads((ROOT/'validation/expected_nets.json').read_text())
xml=ET.parse(ROOT/'validation/netlist.xml').getroot()
actual={n.attrib['name'].lstrip('/'):sorted([[nd.attrib['ref'],nd.attrib['pin']] for nd in n.findall('node')]) for n in xml.find('nets')}
assert actual=={k:sorted(v) for k,v in expected.items()}
for ref,c in COMP.items():
    assert {d.GetNumber():str(d.GetNetname()).lstrip('/') for d in fps[ref].Pads()}==c['nets'],ref
    assert (fps[ref].GetLayer()==p.B_Cu)==(c['side']=='B'),ref
for ref,plus,minus in [('C1','VREF','GND'),('C2','VDD','GND'),('C6','OUTA_C','HP_L'),('C7','OUTB_C','HP_R')]:assert COMP[ref]['nets']=={'1':plus,'2':minus}
assert COMP['U1']['nets']=={'1':'OUTA_RAW','2':'INV_A','3':'VREF','4':'GND','5':'VREF','6':'INV_B','7':'OUTB_RAW','8':'VDD'}
tracks=list(b.GetTracks());assert not any(isinstance(t,p.PCB_VIA) for t in tracks)
assert all(t.GetLayer()==p.B_Cu for t in tracks)
front_pads=[(ref,d.GetNumber()) for ref,fp in fps.items() for d in fp.Pads() if d.FlashLayer(p.F_Cu)]
assert not front_pads,front_pads
assert all(d.FlashLayer(p.B_Cu) for fp in fps.values() for d in fp.Pads())
proof=ROOT/f'validation/empty_front_copper_proof/{NAME}-F_Cu.gtl'
assert proof.exists() and not re.search(r'D0[13]\*|%ADD',proof.read_text(encoding='ascii'))
mask=(ROOT/f'gerbers/{NAME}-B_Mask.gbs').read_text(encoding='ascii')
assert '%LPC' not in mask and 'D03' not in mask,'Review mask parser for new aperture/polarity style'
regions=[]
for region in re.findall(r'G36\*(.*?)G37\*',mask,re.S):
    points=[(int(x)/1e6,int(y)/1e6) for x,y in re.findall(r'X(-?\d+)Y(-?\d+)D0[12]\*',region)]
    if points:regions.append(points)
def inside(x,y,points):
    result=False
    for i in range(len(points)):
        a,z=points[i-1],points[i]
        if (a[1]>y)!=(z[1]>y) and x<(z[0]-a[0])*(y-a[1])/(z[1]-a[1])+a[0]:result=not result
    return result
masked=[];pad_count=0
for ref,fp in fps.items():
    for d in fp.Pads():
        pad_count+=1;pos=d.GetPosition();x=p.ToMM(pos.x);y=-p.ToMM(pos.y)
        if not any(inside(x,y,points) for points in regions):masked.append([ref,d.GetNumber()])
assert not masked,('Soldermask covers pad center',masked)
(ROOT/'validation/soldermask_pad_openings.json').write_text(json.dumps(dict(checked_pad_count=pad_count,covered_pad_centers=masked,result='PASS',method='Point-in-region check of actual bottom-mask Gerber for every native pad center'),indent=2),encoding='utf-8')
def reachable(mode,src,dst):
    g=collections.defaultdict(set)
    for ref,c in COMP.items():
        if ref.startswith('R'):a,z=c['nets']['1'],c['nets']['2']
        elif ref.startswith('JP'):a,z=c['nets']['2'],c['nets']['1' if mode=='ATTENUATED' else '3']
        else:continue
        g[a].add(z);g[z].add(a)
    todo=[src];seen=set()
    while todo:
        s=todo.pop()
        if s in seen:continue
        seen.add(s);todo.extend(g[s]-seen)
    return dst in seen
for src,sel in [('LIN','INA'),('RIN','INB')]:
    assert reachable('ATTENUATED',src,sel) and reachable('ATTENUATED',src,'GND')
    assert reachable('DIRECT',src,sel) and not reachable('DIRECT',src,'GND')
assert not reachable('ATTENUATED','OUTA_RAW','HP_L')
assert not reachable('ATTENUATED','OUTB_RAW','HP_R')
drills=collections.Counter(round(p.ToMM(d.GetDrillSize().x),3) for fp in fps.values() for d in fp.Pads() if d.GetDrillSize().x)
metrics=dict(revision='R2',kicad_version=p.GetBuildVersion(),nominal_board_mm=[50,35,1.6],physical_copper_sides=1,kicad_formal_copper_layers=b.GetCopperLayerCount(),used_copper_layer='B.Cu',front_active_copper_pads=0,front_copper_tracks=0,front_copper_zones=0,front_gerber_apertures_and_draws=0,via_count=0,routing_wire_bridge_count=0,mode_solder_selectors=4,installed_resistors=14,installed_capacitors=7,installed_ICs=1,total_installed_components=22,footprints=len(fps),track_segments=len(tracks),track_widths_mm=sorted(set(round(p.ToMM(t.GetWidth()),3) for t in tracks)),copper_clearance_rule_mm=.25,ground_pour_minimum_thickness_mm=.4,ground_pour_connection='solid',ground_pour_contours=next(iter(b.Zones())).GetFilledPolysList(p.B_Cu).OutlineCount(),component_hole_count=sum(drills.values()),drill_diameters_and_counts_mm=dict(drills),required_fabrication_hole_plating='NONE - all component holes are unplated',schematic_pin_map='PASS',input_modes_and_divider_isolation='PASS',capacitor_polarities='PASS',remaining_physical_checks=['Salvaged TDA1308 operation','Actual capacitor dimensions, ESR and leakage','Real cable stability and analog noise/distortion','Enclosure clearance on both faces'])
(ROOT/'validation/engineering_audit.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
(ROOT/'validation/netlist_check.txt').write_text('PASS: every native PCB pad net matches schematic XML and independent circuit pin map; U1 pins, electrolytic polarities, input selector modes and divider isolation checked.\n',encoding='utf-8')
with (ROOT/'BOM.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f,delimiter=';');w.writerow(['References','Value','Footprint','Quantity','Mounting_side','Install','Notes'])
    groups=collections.defaultdict(list)
    for ref,c in COMP.items():
        if ref.startswith(('J','TP')):continue
        groups[c['value'],c['fp'],c['side']].append(c)
    for (v,fp,side),cs in groups.items():w.writerow([','.join(c['ref'] for c in cs),v,fp,len(cs),'BOTTOM' if side=='B' else 'TOP','YES',' | '.join(c['ref']+': '+c['role'] for c in cs)])
    w.writerow(['JP1-JP4','Mode solder selector','Jumper:SolderJumper-3_P1.3mm_Open_Pad1.0x1.5mm',4,'BOTTOM','SOLDER ONLY','No resistor or connector; default bridge 1-2, DIRECT bridge 2-3 on BOTH selectors of a channel'])
    w.writerow(['J1-J7','Wire pads','MonoPod_R2:SolderWire-0.5sqmm_1x01_D0.9mm_OD2.1mm_SingleSide',7,'BOTTOM COPPER','PCB ONLY','Drill 1.15mm, pad 2.15mm; wire AWG26-30'])
    w.writerow(['TP1-TP9','Test pads','TestPoint:TestPoint_Pad_D1.5mm',9,'BOTTOM','PCB ONLY','No purchased part; no drilled hole'])
with (ROOT/'assembly_coordinates.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f,delimiter=';');w.writerow(['Ref','Value','X_mm_top_view','Y_mm_top_view','KiCad_rotation_deg','Mounting_side'])
    for ref,fp in fps.items():w.writerow([ref,fp.GetValue(),p.ToMM(fp.GetPosition().x),p.ToMM(fp.GetPosition().y),fp.GetOrientationDegrees(),'BOTTOM' if fp.GetLayer()==p.B_Cu else 'TOP'])
rl=1/(1/32+1/100000)
att=(1/(1/3300+1/3900))/(10000+1/(1/3300+1/3900))
calc=dict(attenuator_loaded_midband=att,attenuator_loaded_db=20*math.log10(att),total_midband_gain_with_2R2=att*rl/(rl+2.2),headphone_Vrms_for_2_1Vrms_input=2.1*att*rl/(rl+2.2),active_resistor_ratio=-1,input_corner_Hz_att_for_4_7u=1/(2*math.pi*4.7e-6*(3900+1/(1/10000+1/3300))),input_corner_Hz_direct_for_4_7u=1/(2*math.pi*4.7e-6*3900),output_corner_Hz_no_series={str(c):1/(2*math.pi*c*1e-6*32) for c in [100,220,470]},output_corner_Hz_with_2R2={str(c):1/(2*math.pi*c*1e-6*(32+2.2)) for c in [100,220,470]})
(ROOT/'validation/calculations.json').write_text(json.dumps(calc,indent=2),encoding='utf-8')
readme='''# MonoPod TDA1308, R2: односторонняя плата

KiCad 10.0.6, 27.10.2026. Нативные файлы `TDA1308_HeadphoneAmp.kicad_pro`, `.kicad_sch` и `.kicad_pcb` являются редактируемыми оригиналами. Библиотеки и 3D-модели находятся в `libraries/`, переносить папку проекта целиком.

## Изменения относительно R1

| Параметр | R1 | R2 |
|---|---|---|
| Размер платы | 45 x 25 мм | 50 x 35 мм |
| Физические медные стороны | 2 | 1, только нижняя B.Cu |
| Переходные отверстия | 14 | 0 |
| Проволочные перемычки для разводки | 0 | 0 |
| Ширина дорожек | 0,25 мм | 0,60 мм |
| Устанавливаемые резисторы | 18, 0805 | 14, 1206 |
| Нулевые резисторы | 4 установлены + 2 места DNP | 0 |
| Керамические конденсаторы C3-C5 | SMD | Все выводные |
| Устанавливаемые электронные детали | 26 | 22 |

Медь, TDA1308, резисторы и селекторы JP находятся снизу. Все семь конденсаторов стоят сверху, их выводы паяются к нижней меди. Отверстия компонентов не являются переходными отверстиями. В R2 всего 21 отверстие для выводов/проводов: 14 x 0,8 мм и 7 x 1,15 мм. Для физической односторонней платы металлизация отверстий не требуется.

KiCad поддерживает формально чётное число медных слоёв, поэтому в редакторе сохранён служебный двухслойный стек. Верхний F.Cu электрически и физически не используется: нет дорожек, полигонов и активных колец площадок. Для выводных площадок в локальной библиотеке включено удаление всех неподключённых колец, включая крайние слои. Нативный контрольный экспорт F.Cu пуст: `validation/empty_front_copper_proof/`. Верхний медный Gerber отсутствует в производственном архиве. Это одна физическая сторона меди, а не скрытая двухслойная разводка. См. [документацию KiCad](https://docs.kicad.org/10.0/en/pcbnew/pcbnew.html).

Предыдущая R1 сохранена в `revisions/` основной рабочей папки на I:. Для изготовления новой версии использовать только архив R2 из текущей папки.

## Электрическая схема и сокращение резисторов

Основа сохранена: [NXP TDA1308, Rev.5, Fig.4, стр.6](https://www.nxp.com/docs/en/data-sheet/TDA1308.pdf). VREF = VDD/2, два инвертирующих каскада с Rin=Rf=3,9 кОм, обязательные выходные разделительные электролиты, развязка питания и общий GND. Электрические номиналы R1-R14 и C1-C7 относительно R1 не изменены.

Удалены R15-R20: четыре устанавливаемых нулевых резистора и два резервных места. Вместо них четыре трёхконтактных паяных селектора JP1-JP4. Резисторы делителя VREF, входных делителей и обратной связи нужны для сохранения рабочей схемы. R11/R12 = 2,2 Ом оставлены для выходной развязки с неизвестным кабелем; R13/R14 = 100 кОм сохраняют определённый потенциал отключённого выхода. Их удаление изменило бы поведение и диагностику платы, поэтому дальнейшее сокращение не выполнено.

### Режим ATT/DIRECT

Все JP изготовлены РАЗОМКНУТЫМИ. Перед включением необходимо запаять выбранные пары. Это четыре припойные перемычки выбора режима, они не используются для пересечения дорожек. Отдельных проводов и нулевых резисторов для разводки нет.

| Канал | Селекторы | ATTENUATED, штатно для PCM5102A | DIRECT для слабого источника |
|---|---|---|---|
| Левый | JP1 и JP2 | На обоих соединить 1-2 | На обоих убрать 1-2 и соединить 2-3 |
| Правый | JP3 и JP4 | На обоих соединить 1-2 | На обоих убрать 1-2 и соединить 2-3 |

Вывод 1 отмечен треугольником на нижней шелкографии; сверяться с нижним assembly-видом. Нельзя соединять все три площадки и нельзя оставлять два селектора одного канала в разных режимах. Переключать только при снятом питании. В DIRECT делитель отключён от источника и от выбранного входа с обоих концов, паразитной нагрузки 3,3 кОм нет. Контроль DC-графа обоих режимов выполнен в `scripts/document.py`.

В ATT реальное ослабление с нагрузкой входа 3,9 кОм составляет около -16,4 дБ, а не -12 дБ изолированного делителя. С выходным 2,2 Ом на 32 Ом получается около -17 дБ и 0,298 В RMS при 2,1 В RMS от PCM5102A, приблизительно 2,8 мВт/канал. Это расчёт линейной схемы, не измеренная мощность. DIRECT при полной амплитуде PCM5102A вызовет клиппинг: начинать с малой цифровой громкости.

## Конденсаторы: все выводные, все сверху

| Ref | Номинал | Корпус и шаг | Полярность |
|---|---|---|---|
| C1 | 22 мкФ / >=10 В | Электролит, D<=5 мм, P=2 мм | + к VREF, - к GND |
| C2 | 100 мкФ / >=10 В | Электролит, D<=6,3 мм, P=2,5 мм | + к VDD, - к GND |
| C3 | 100 нФ / >=25 В X7R | Выводной керамический, D<=5 мм, толщина <=2,5 мм, P=2,5 мм | Неполярный |
| C4/C5 | 4,7 мкФ / >=25 В X7R | Выводной многослойный керамический, тело <=7 x 4,5 мм, P=5 мм | Неполярные |
| C6/C7 | 220 мкФ / >=10 В | Электролит, D<=8 мм, P=3,5 мм | + к усилителю через R11/R12, - к наушникам |

Керамические C4/C5 должны иметь эффективную ёмкость >=2,2 мкФ при постоянном смещении до 2,5 В. Обычные диски 100 нФ вместо них не подходят: это сильно поднимет нижнюю границу полосы. Выбирать X7R, не Y5V/Z5U. Подходящий класс деталей существует среди выводных MLCC, например семейство [KEMET Goldmax X7R](https://content.kemet.com/datasheets/KEM_C1050_GOLDMAX_X7R.pdf); перед покупкой проверить конкретный корпус, шаг и DC bias. Шаг 5,08 мм можно аккуратно привести к 5,00 мм. 3D-модель показывает габарит посадочного места, а не гарантированный цвет/форму покупного конденсатора.

Большие выходные ёмкости остаются электролитическими: замена на малые керамические диски нарушила бы НЧ-характеристику. В C6/C7 можно установить 470 мкФ / >=10 В в том же габарите или временно имеющиеся 100 мкФ / 25 В. Срез на 32 Ом без учёта 2,2 Ом: 49,7 / 22,6 / 10,6 Гц для 100 / 220 / 470 мкФ. С 2,2 Ом: 46,5 / 21,2 / 9,9 Гц. На 20 Гц у 220 мкФ остаётся заметный спад; 470 мкФ лучше сохраняет бас.

Ваши выпаянные 100 мкФ / 25 В подходят для C2 при D<=6,3 мм и для временной замены C6/C7 при D<=8 мм. Если шаг 2,5 мм, выводы можно осторожно сформовать до 3,5 мм, не изгибая их у пробки. Их высота, ESR, утечка и исправность неизвестны. Для штатного варианта дополнительно нужны 22 мкФ x1, 100 нФ выводной X7R x1, 4,7 мкФ выводной X7R x2 и 220/470 мкФ x2. C2 можно взять из имеющегося набора после проверки.

## Подключение и монтаж

Штатное питание 3,3 В; 5 В только после полного стендового испытания. Не подавать 5 В на ESP32. Плата не содержит преобразователей, стабилизаторов или защиты от переполюсовки. Общая земля объединяет PCM5102A, питание и наушники. Разъём 3,5 мм вынесен на провода.

| Площадка | Маркировка | Подключение |
|---|---|---|
| J1 | VCC | Питание 3,3 В |
| J2 | GND | Земля источника питания/PCM5102A |
| J3 | LIN | Левый сигнал PCM5102A |
| J4 | RIN | Правый сигнал PCM5102A |
| J5 | GND | Общий контакт наушников |
| J6 | LOUT | Левый наушник, после C6 |
| J7 | ROUT | Правый наушник, после C7 |

Маркировка проводов находится на нижней стороне. При перевороте платы левое/правое положение на изображении меняется: подключать по надписям, не по памяти о R1. Площадки проводов 2,15 мм, отверстия 1,15 мм подходят для AWG26-30. Механическую разгрузку проводов выполнить в корпусе; монтажных отверстий нет.

Сначала паять SMD снизу: U1, резисторы и выбранные пары JP. Затем устанавливать конденсаторы сверху и паять их ножки снизу, подрезая выводы. C3 располагать как можно ближе к плате: длинные ножки ухудшают высокочастотную развязку. Точки GND имеют сплошное соединение с заливкой, поэтому при ручной пайке понадобится хороший теплоотвод от жала и флюс; не компенсировать недостаток мощности длительным перегревом деталей.

U1 остаётся SOIC-8 3,9 x 4,9 мм с шагом 1,27 мм. На нижнем виде pin 1 находится сверху справа от корпуса; ориентироваться по точке корпуса и метке шелкографии. TSSOP-8 не подходит. Все полярные конденсаторы имеют + на квадратной площадке, к усилителю/питанию; закрашенная половина силуэта обозначает отрицательную сторону. Проверить ориентацию по assembly-изображениям.

Снизу к высоте платы добавляются SMD-компоненты: место и изоляцию от металлического корпуса нужно проверить. Высота старых электролитов и совместимость корпуса MonoPod не измерены. Рисунки 3D библиотечные; они не подтверждают реальные габариты вашего набора.

## Тестовые площадки

TP1 VDD; TP2 GND; TP3 VREF; TP4 INA; TP5 INB; TP6 OUTA_RAW; TP7 OUTB_RAW; TP8 HP_L; TP9 HP_R. Все площадки находятся снизу и не требуют отдельных компонентов. INA/INB находятся после селекторов, до входных конденсаторов. RAW имеют постоянное смещение около VREF и не предназначены для наушников.

Схема сохраняет обратную связь до разделительных конденсаторов. Не подключать большие керамические конденсаторы от RAW к GND. В даташите CL ограничена 200 пФ при паспортных условиях. 2,2 Ом не гарантируют устойчивость с любым кабелем: проверить осциллографом на 330/33 Ом и с реальным кабелем. Noise gain около 1,61 в ATT и 2 в DIRECT; электрические значения относительно R1 не изменены.

'''
first=(ROOT/'docs/first_power_on.md').read_text(encoding='utf-8')
readme+=first
readme+='''
## Проверки и файлы изготовления

Нативные ERC и DRC выполнены со всеми уровнями замечаний, с проверкой соответствия PCB схеме: 0 нарушений, 0 неразведённых соединений и 0 расхождений со схемой. Исключения DRC не добавлялись. Карта всех выводов и нетлист сверены, полярности и конфигурации селекторов проверены. В частности, на F.Cu нет активных площадок, дорожек или заливки; контрольный Gerber F.Cu содержит ноль апертур/рисующих команд. Все дорожки B.Cu имеют ширину 0,6 мм, правило зазора 0,25 мм; минимальная толщина перемычек заливки земли 0,4 мм.

`manufacturing.zip` содержит только одну медь B.Cu, нижнюю маску, шелкографию двух сторон, контур, единый Excellon с 21 отверстием и `FABRICATION.txt`. Для физического заказа: ОДНА сторона меди снизу, FR4 1,6 мм, медь 35 мкм, все отверстия БЕЗ металлизации. Финиш HASL или иной стандартный паяемый. Не создавать верхнюю медь, не зеркалить и не масштабировать Gerber. Если сервис предлагает только двухслойный заказ, выбрать изготовителя с поддержкой односторонних плат или явно согласовать пустую верхнюю сторону; заказ автоматически не выполнялся.

Использован обычный Gerber RS-274X без X2-атрибутов. Комментарии X2 со служебной двухслойной нумерацией удалены; все графические команды побайтно по строкам сохранены, контроль в `gerber_metadata_normalization.json`. Внутренний двухслойный Gerber job KiCad намеренно не включён. Excellon имеет минимальный заголовок без атрибута металлизации; физическое отсутствие металлизации явно задано в FABRICATION.txt. Нативный отчёт KiCad называет выводные отверстия PTH по электрическому типу площадок — это служебная классификация CAD, а не указание металлизировать эту одностороннюю плату.

`BOM.csv` содержит 14 резисторов, 7 конденсаторов и одну U1 — всего 22 устанавливаемые детали. JP/J/TP указаны отдельно как площадки без покупных компонентов. Координаты и фактические повороты: `assembly_coordinates.csv`. В `renders/` находятся виды сверху/снизу, assembly и медь. `docs/schematic.pdf` — обзор, `.kicad_sch` — редактируемая схема. `validation/` содержит результаты, расчёты, карту цепей, доказательство пустого F.Cu и SHA-256.

Изготовленный физический образец не испытан. Чистые ERC/DRC не подтверждают исправность выпаянной TDA1308, утечку старых электролитов, реальные THD/шум или устойчивость с кабелем. Перенос библиотеки и native-паритет проверяются отдельно. Для редактирования открыть основной `.kicad_pro`. Скрипты `design.py`/`build_pcb.py` перезаписывают CAD; обычные правки вносить в KiCad. Повторные проверки: bundled Python KiCad + `scripts/validate.py`, экспорт — `scripts/export.py`.
'''
(ROOT/'README.md').write_text(readme,encoding='utf-8')
(ROOT/'FABRICATION.txt').write_text('TDA1308 HeadphoneAmp R2 - SINGLE COPPER SIDE\nNominal outline centerline: 50 x 35 mm. FR4 1.6 mm, copper 35 um.\nONE physical copper foil: BOTTOM ONLY (B_Cu.gbl). NO TOP COPPER.\nALL 21 COMPONENT HOLES UNPLATED: 14 x 0.80 mm + 7 x 1.15 mm.\nNO VIAS. NO ROUTING WIRE JUMPERS.\nSolder mask bottom; silkscreen both sides. Standard solderable finish (HASL).\nTracks 0.60 mm, copper clearance 0.25 mm; GND pour minimum neck 0.40 mm.\nExcellon is metric with minimal header; coordinates exported directly by KiCad.\nUse Gerbers as supplied: do not mirror or scale. Edge_Cuts is routed outline.\nKiCad internal project uses two logical layers; ignore that software-only stackup.\nThe supplied fabrication set contains exactly ONE copper Gerber.\nNo board assembly/order has been requested.\n',encoding='ascii')
(ROOT/'validation/datasheet_comparison.md').write_text('''# R2 electrical comparison

Primary: https://www.nxp.com/docs/en/data-sheet/TDA1308.pdf , Rev.5, Fig.4 p.6; pinout p.3; characteristics p.5; SO8 p.9.

Retained: pinout 1 OUTA / 2 INA- / 3 INA+ / 4 GND / 5 INB+ / 6 INB- / 7 OUTB / 8 VDD. Divider 10k+10k, CREF 22u, bulk 100u, bypass 100n, inverting channels with Rin=Rf=3.9k, output electrolytic + toward driver.

R2 retains every analog R1 value R1-R14 and C1-C7. R15-R20 link footprints are replaced with four three-pad open solder selectors. Default ATT closes 1-2; DIRECT closes 2-3 on both selectors of a channel and isolates the unused divider at both ends. All ceramics are now leaded; electrodes and values unchanged. Existing 2.2R output isolation and 100k bleeds retained. Copper is only on B.Cu; SMD on bottom, THT capacitors on top. The 100n capacitor has a short connection to pin8 and trimmed leads are required.

This is static/CAD verification, not physical stability, THD or output-power verification. The single-side layout, unknown salvaged IC and real headphone cable must be checked on the bench as documented. No extra output capacitive load or uncalculated RC filter was added.
''',encoding='utf-8')
print(json.dumps(metrics,indent=2));print('R2 documentation, BOM and native pin checks: PASS')
