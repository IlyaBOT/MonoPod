"""Export every editable parameter with units, provenance and fit status."""
from pathlib import Path
import argparse
import csv
import json
import build_enclosure as b


def flatten(value, path=''):
    if isinstance(value, dict):
        for key, item in value.items(): yield from flatten(item, path+'.'+key if path else key)
    else: yield path, value


def provenance(path):
    if path in ['version','units']:
        return 'Служебный','Формат проекта'
    if path.startswith('case.'):
        return 'ПРОЕКТНОЕ','Начальные габариты и конструкция; подогнать после обмера начинки и пробников'
    if path.startswith('acrylic.'):
        if path in ['acrylic.stock_width','acrylic.stock_height','acrylic.thickness']:
            return 'НОМИНАЛ ИЗ ЗАДАНИЯ','Лист акрила пользователя 200x100x1; фактическую толщину измерить перед вклейкой'
        return 'ПРОЕКТНОЕ / ПРОБНИК','Основной вариант: плоский акрил почти вровень и задняя клеевая рамка. front_lip/stop_pad используются только альтернативой rear_lip. Контуры лазера номинальные, пропил в CAM'
    if path.startswith('lens_carrier.'):
        return 'ПРОЕКТНОЕ / ПРОБНИК','Печатная задняя рамка: полоса клея 2, толщина 0.8, общие M2.5x5 с TFT. Сухая примерка и кондуктор перед вклейкой'
    if path.startswith('usb_cable.'):
        if path in ['usb_cable.body_width','usb_cable.body_height','usb_cable.metal_tip_length']:
            return 'ПРИМЕРНО ИЗМЕРЕНО ПОЛЬЗОВАТЕЛЕМ','02.11.2026: корпус 9.7x5.6, металлический носик около 7. Систематическая погрешность штангенциркуля не измерена'
        if path in ['usb_cable.body_length','usb_cable.metal_width','usb_cable.metal_height','usb_cable.engagement_depth']:
            return 'НЕ ОБМЕРЕНО / ГАБАРИТ','Глубина сопряжения 6 мм и сечение металла условные; геометрический резерв не подтверждает электрический контакт. Проверить настоящим кабелем'
        return 'ПРОЕКТНОЕ / ПРОБНИК','0.4 зазора на сторону, 0.5 осевого резерва. В тесте суммарный размер корпуса увеличен на 0.2, носик укорочен на 0.2. Это допуск модели, не калибровка прибора'
    if path.startswith('wheel.'):
        if path=='wheel.lug_radius': return 'ПО ИСХОДНОЙ CAD','Наружный радиус уха из генератора v0.3'
        return 'ПРОЕКТНОЕ ПОД ИНТЕРФЕЙС','Сопряжение с неизменённым Click Wheel v0.3; исходные 42x42/2.8/1.15 фиксируются отдельными reference/STEP/JSON'
    if path.startswith('display.'):
        if path=='display.rotation_degrees': return 'ПРОЕКТНОЕ','90 градусов: альбомный экран под интерфейс 160x128; реальные отверстия и стекло не обмерены'
        return 'НЕ ПОДТВЕРЖДЕНО','Габаритный профиль типового TFT; проверить display_window_coupon и изменить по своей плате'
    if path.startswith('mainboard.'):
        if path in ['mainboard.width','mainboard.height']:
            return 'ИЗМЕРЕНО ПОЛЬЗОВАТЕЛЕМ','Макетная плата 70x90 мм; уточнение 01.11.2026'
        if path=='mainboard.stack_to_wheel_face':
            return 'ОЦЕНКА ПОЛЬЗОВАТЕЛЯ С ЗАПАСОМ','От низа платы/пайки до лица колеса около 20 мм; для модели выбрано 22 мм. Проверить осевой пробник'
        if path=='mainboard.mount_from_left_bottom':
            return 'НЕ ОБМЕРЕНО','Три стартовых отверстия от ЛЕВОГО НИЖНЕГО края платы. Не сверлить плату по этому файлу; проверить реальную разводку и края'
        return 'ПРОЕКТНОЕ / ОБМЕРИТЬ','Общий родитель всей припаянной сборки; jack_tip_recess сдвигает плату и оба разъёма вместе, сохраняя защиту носика джека'
    if path in ['tp4056.pcb_width','tp4056.pcb_length','tp4056.module_height']:
        return 'НОМИНАЛ ИЗ ЗАДАНИЯ','28x17x4 дано пользователем; выступ USB/толщина PCB/реальная высота требуют примерки'
    if path.startswith('tp4056.'):
        if path in ['tp4056.usb_left_from_board_left','tp4056.usb_tip_beyond_board']:
            return 'ПРИМЕРНО ИЗМЕРЕНО ПОЛЬЗОВАТЕЛЕМ','Диапазон 6–7 мм; выбран центр 6.5. Перед финальным корпусом обмерить'
        return 'ПРОЕКТНОЕ / ПРИМЕРИТЬ','Высоты над платой и размер USB не обмерены. Геометрия привязана к mainboard; отдельного кармана/прижима TP4056 нет'
    if path.startswith('connector_insert.'):
        return 'ПРОЕКТНОЕ / ПРОБНИК','Съёмная нижняя вставка для всей припаянной сборки; M2x4 снаружи. Проверить bottom_edge_coupon и высоты осей'
    if path.startswith('jack.'):
        if path in ['jack.gap_from_usb_right','jack.tip_beyond_board']:
            return 'ПРИМЕРНО ИЗМЕРЕНО ПОЛЬЗОВАТЕЛЕМ','Зазор около 8 мм между металлическими краями; выступ 7–8 мм до конца резьбы. Высота оси не обмерена'
        exact = ['body_width','body_length','body_height','body_x_offset','axis_above_body_bottom','thread_diameter','thread_length','bore_diameter','pin_length']
        if path.split('.')[-1] in exact:
            return 'ЧЕРТЁЖ АНАЛОГА','XKB PJ-324MX A1, 10.09.2024; соответствие вашей детали не подтверждено'
        if path in ['jack.nut_diameter','jack.nut_thickness']:
            return 'НЕ ПОДТВЕРЖДЕНО','Габарит гайки оценён; проверить ваш экземпляр и полное зацепление'
        return 'ПРОЕКТНОЕ / ПРИМЕРИТЬ','Посадка/положение подбираются jack/bottom coupons'
    if path.startswith('hold.'):
        return 'НЕ ПОДТВЕРЖДЕНО / ПРИМЕРИТЬ','Семейство SS22F32 либо компактный аналог; тип на фото не установлен. Есть отдельные шаги отверстий, окна и карманы'
    if path.startswith('fasteners.'):
        if path.endswith('_screw'):
            return 'ИЗ НАБОРА ПОЛЬЗОВАТЕЛЯ','Выбран доступный винт; допустимость длины зависит от приведённого стека деталей'
        if 'head_' in path:
            return 'НЕ ПОДТВЕРЖДЕНО','Головки винтов условные, измерить фактические'
        return 'ПРОЕКТНОЕ / ПРОБНИК','Пилоты и проходные отверстия проверяются под материал и принтер'
    return 'ПРОЕКТНОЕ','Серия калибров, маркировка и точность сетки'


def units(path, value):
    if path in ['version','units'] or isinstance(value,str): return '-'
    if path.endswith('rotation_degrees'): return 'град'
    if path.endswith('stl_angular_tolerance'): return 'рад'
    if path.endswith('_count'): return 'шт'
    return 'мм'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parameters',type=Path,default=b.HERE/'parameters.json')
    parser.add_argument('--output',type=Path,default=b.ROOT)
    args=parser.parse_args(); root=args.output.resolve()
    parameters=json.loads(args.parameters.read_text(encoding='utf-8-sig'))
    rows=[]; metadata={}
    for path,value in flatten(parameters):
        status,note=provenance(path); unit=units(path,value)
        text=json.dumps(value,ensure_ascii=False) if isinstance(value,list) else str(value)
        rows.append([path,text,unit,status,note]);metadata[path]={'value':value,'units':unit,'status':status,'note':note}
    with (root/'PARAMETERS.csv').open('w',encoding='utf-8-sig',newline='') as stream:
        writer=csv.writer(stream);writer.writerow(['parameter','value','units','status','source_or_test']);writer.writerows(rows)
    lines=[f'# Параметры MonoPod Enclosure v{parameters["version"]}\n',
        '\nВсе редактируемые значения находятся в `source/parameters.json`. Координаты: X вправо, Y вверх, глубина D=-Z. Зазоры в серии - **на сторону**, если не указано иное. Размеры display задаются до поворота; `rotation_degrees=90` поворачивает окно, плату, стекло и сетку креплений вместе.\n',
        '\nИзменение JSON требует повторного запуска генератора. Параметры исходного колеса сохранены отдельно в `reference/clickwheel/source/parameters.json`; не меняйте их для подгонки корпуса.\n',
        '\n| Параметр | Значение | Ед. | Статус | Источник / проверка |\n|---|---|---|---|---|\n']
    for row in rows:
        lines.append('| '+' | '.join(str(text).replace('|','/') for text in row)+' |\n')
    (root/'PARAMETERS_RU.md').write_text(''.join(lines),encoding='utf-8')
    (root/'validation/parameter_status.json').write_text(json.dumps(metadata,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print('Parameter table:',len(rows),'entries',flush=True)


if __name__=='__main__':main()
