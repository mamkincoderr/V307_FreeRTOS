# Рисунок платы CH32V307V-R1-1v0: свободные каналы АЦП, VDDA и GND на разъёмах.
# Плата повёрнута разъёмом Ethernet вниз (на 90° по часовой относительно фото).
# Выход: pinout_adc_free.svg (вектор) и pinout_adc_free.png (растр, через headless Edge).
# Запуск: python -I docs\pinout_free_gen.py
import html, os, subprocess

D = os.path.dirname(os.path.abspath(__file__))
J3 = {1:'VCC',2:'VCC',3:'BOOT1',4:'BOOT0',5:'GND',6:'GND',7:'VCC',8:'VCC',9:'GND',10:'NC',11:'ELED1',12:'VCC',
      13:'ELED2',14:'NRST',15:'VCC',16:'VCC',17:'+5V',18:'+5V',19:'LED1',20:'GND',21:'LED2',22:'GND',23:'KEY',
      24:'+5V',25:'GND',26:'GND',27:'PA14',28:'PA0',29:'PA13',30:'PA1',31:'PC0',32:'PA2',33:'PC1',34:'PA3',35:'PC2',
      36:'PA4',37:'PC3',38:'PA5',39:'PC4',40:'PC5',41:'PB2',42:'PE7',43:'PE8',44:'PE9',45:'PE10',46:'PE11',47:'PE12',
      48:'PE13',49:'PE14',50:'PE15',51:'PD8',52:'PD9',53:'PD10',54:'PD11'}
J4 = {1:'PB10',2:'PC13',3:'PB11',4:'VBAT',5:'VDDA',6:'PE6',7:'GND',8:'PE5',9:'PB13',10:'PE4',11:'PB14',12:'PE3',
      13:'PB15',14:'PE2',15:'PB12',16:'PE1',17:'PB0',18:'PE0',19:'PB1',20:'PB7',21:'PD6',22:'PB6',23:'PB9',24:'PB4',
      25:'PB8',26:'PB3',27:'PB5',28:'PD7',29:'PA6',30:'PD5',31:'PA7',32:'PD4',33:'PA8',34:'PD3',35:'PA9',36:'PD2',
      37:'PA10',38:'PD1',39:'PD0',40:'PC12',41:'PC11',42:'PC10',43:'PA15',44:'NC',45:'PA12',46:'PA11',47:'PC9',
      48:'PC8',49:'PC7',50:'PC6',51:'PD15',52:'PD14',53:'PD13',54:'PD12'}
J5 = {1:'NC',2:'VCC',3:'NRST',4:'VCC',5:'+5V',6:'GND',7:'GND',8:'+5V'}
J6 = {i + 1: 'PA%d' % i for i in range(6)}
J7 = {1:'PB10',2:'PB11',3:'VDDA',4:'GND',5:'PB13',6:'PB14',7:'PB15',8:'PB12',9:'PB0',10:'PB1'}
J8 = {1:'PB9',2:'PB8',3:'PB5',4:'PA6',5:'PA7',6:'PA8',7:'PA9',8:'PA10'}
ARD = {('J6', i + 1): 'A%d' % i for i in range(6)}
ARD.update({('J7', 9): 'D9', ('J7', 10): 'D8', ('J8', 4): 'D4', ('J8', 5): 'D3'})

ADC = {'PA%d' % i: i for i in range(6)}            # в работе — только левая сторона: PA0–PA5 (J6/J3) и PC0–PC5 (J3)
ADC.update({'PC%d' % i: 10 + i for i in range(6)})

C_ADC, C_VDDA, C_GND = '#28b7ad', '#ef8b45', '#c6d0d2'
PCB, PCB_LINE, HOLE, TXT, MUT = '#315b4b', '#7e9b88', '#20382f', '#edf2ec', '#b7c7bd'

OX = 240                       # левый край платы
def X(y_orig): return OX + 660 - (y_orig - 70)
def Y(col): return 350 + col * 28

S = []
a = S.append
W, H = 1180, 1330
a('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="Consolas, \'IBM Plex Mono\', monospace">' % (W, H, W, H))
a('<rect width="%d" height="%d" fill="#20262b"/>' % (W, H))
a('<text x="40" y="44" font-size="22" font-weight="bold" fill="%s" font-family="Segoe UI, Arial, sans-serif">CH32V307V-R1-1v0 — каналы АЦП (левая сторона), VDDA и GND</text>' % TXT)
a('<text x="40" y="70" font-size="14" fill="%s" font-family="Segoe UI, Arial, sans-serif">Вид сверху, разъём Ethernet внизу. Каналы IN0–IN5, IN10–IN15 + внутренние IN16, IN17.</text>' % MUT)

# плата
a('<rect x="%d" y="90" width="660" height="1160" rx="14" fill="%s"/>' % (OX, PCB))
a('<line x1="%d" y1="290" x2="%d" y2="290" stroke="%s" stroke-width="2" stroke-dasharray="7 6"/>' % (OX, OX + 660, PCB_LINE))
a('<text x="%d" y="278" font-size="12" fill="%s" text-anchor="middle">WCH-Link (отламывается по линии)</text>' % (OX + 330, PCB_LINE))
def part(x, y, w, h, label, sub=''):
    a('<rect x="%d" y="%d" width="%d" height="%d" rx="4" fill="#294a3d" stroke="%s"/>' % (x - w / 2, y - h / 2, w, h, PCB_LINE))
    a('<text x="%d" y="%d" font-size="12" fill="#edf2ec" text-anchor="middle">%s</text>' % (x, y + 4, label))
    if sub: a('<text x="%d" y="%d" font-size="11" fill="%s" text-anchor="middle">%s</text>' % (x, y + h / 2 + 15, PCB_LINE, sub))
part(X(395), 120, 70, 40, 'P9 USB')
part(X(270), 205, 80, 40, 'CH549')
part(X(632), 205, 130, 40, 'J2 SWD+UART')
a('<rect x="%d" y="635" width="150" height="150" rx="6" fill="#294a3d" stroke="%s"/>' % (X(395) - 75, PCB_LINE))
a('<text x="%d" y="700" font-size="16" fill="#edf2ec" text-anchor="middle">CH32V307</text>' % X(395))
a('<text x="%d" y="720" font-size="11" fill="%s" text-anchor="middle">VCT6 · LQFP100</text>' % (X(395), PCB_LINE))
part(X(390), 1190, 120, 100, 'RJ45', '')
a('<text x="%d" y="1132" font-size="11" fill="%s" text-anchor="middle">Ethernet 10M</text>' % (X(390), PCB_LINE))
part(X(220), 1215, 60, 50, 'USB HS')
part(X(570), 1215, 60, 50, 'USB FS')

def hole(x, y):
    a('<circle cx="%d" cy="%d" r="4" fill="%s"/>' % (x, y, HOLE))
def pin(x, y, color, title):
    a('<circle cx="%d" cy="%d" r="9" fill="%s" stroke="#ffffff" stroke-width="2"><title>%s</title></circle>' % (x, y, color, html.escape(title)))
def kind(n):
    if n in ADC: return C_ADC
    if n == 'VDDA': return C_VDDA
    if n == 'GND': return C_GND
    return None
def text(x, y, s, color, anchor, size=13, bold=False):
    a('<text x="%d" y="%d" font-size="%d" fill="%s" text-anchor="%s"%s>%s</text>' % (
        x, y + 5, size, color, anchor, ' font-weight="bold"' if bold else '', html.escape(s)))
def desc(n):
    return ('IN%d  %s' % (ADC[n], n)) if n in ADC else n

def header2(name, d, x_outer_odd, x_inner_even, xa, anchor, outer_is_odd, show_labels=True):
    # двухрядная гребёнка: выводы 2c+1 и 2c+2 в одной позиции c
    for c in range(27):
        odd, even = 2 * c + 1, 2 * c + 2
        xo = x_outer_odd if outer_is_odd else x_inner_even
        xe = x_inner_even if outer_is_odd else x_outer_odd
        labels = []
        for num, x in ((odd, xo), (even, xe)):
            col = kind(d[num])
            if col: pin(x, Y(c), col, '%s.%d %s' % (name, num, d[num])); labels.append((num, d[num], col))
            else: hole(x, Y(c))
        if labels and show_labels == 'adc':
            only = [n for num, n, col in labels if n in ADC and not n.startswith('PA')]
            if only: text(xa, Y(c), '   '.join(desc(n) for n in only), C_ADC, anchor)
        elif labels and show_labels:
            s = '   '.join('%s.%d %s' % (name, num, desc(n)) for num, n, col in labels)
            text(xa, Y(c), s, labels[0][2], anchor)

def header1(name, d, start_col, x, xa, anchor):
    for k, n in d.items():
        y = Y(start_col + k - 1)
        col = kind(n)
        if col:
            pin(x, y, col, '%s.%d %s' % (name, k, n))
            s = desc(n) + (('  ' + ARD[(name, k)]) if (name, k) in ARD and n in ADC else '')
            text(xa, y, s, {C_ADC: '#66d3ca', C_VDDA: '#ffad70', C_GND: '#c6d0d2'}[col], anchor, 12)
        else:
            hole(x, y)

# J3 слева: нечётный ряд у края (X(690)), чётный внутри (X(662)); подписи слева от платы
header2('J3', J3, X(690), X(662), OX - 18, 'end', True, 'adc')   # подписи только PC0–PC5 (PA0–PA5 подписаны на J6)
header1('J5', J5, 4, X(612), X(612) + 18, 'start')
header1('J6', J6, 13, X(612), X(612) + 18, 'start')
# J4 справа: чётный ряд у края (X(110)), нечётный внутри (X(138)); подписи справа от платы
header2('J4', J4, X(110), X(138), OX + 660 + 18, 'start', False, False)
header1('J7', J7, 0, X(190), X(190) - 18, 'end')
header1('J8', J8, 11, X(190), X(190) - 18, 'end')

def hlabel(x, y, s):
    text(x, y, s, '#edf2ec', 'middle', 13, True)
hlabel(X(676), 330, 'J3'); hlabel(X(612), 330, 'J5'); hlabel(X(612), Y(13) - 22, 'J6')
hlabel(X(124), 330, 'J4'); hlabel(X(190), 330, 'J7'); hlabel(X(190), Y(11) - 22, 'J8')
text(X(690), Y(0) - 22, '1', PCB_LINE, 'middle', 11); text(X(662), Y(0) - 22, '2', PCB_LINE, 'middle', 11)
text(X(138), Y(0) - 22, '1', PCB_LINE, 'middle', 11); text(X(110), Y(0) - 22, '2', PCB_LINE, 'middle', 11)

# легенда и аналоговое питание
LY = 1272
for i, (c, s) in enumerate(((C_ADC, 'канал АЦП'), (C_VDDA, 'VDDA — аналоговое питание 3,3 В'), (C_GND, 'GND (VSSA соединён с общей землёй)'))):
    x = 40 + i * 380
    a('<circle cx="%d" cy="%d" r="8" fill="%s"/>' % (x, LY, c))
    text(x + 16, LY, s, TXT, 'start', 14)
text(40, LY + 32, 'VDDA ← 3,3 В через R25 0 Ω;  VREF+ = VDDA через R26 0 Ω;  VREF− = GND через R23 0 Ω.  Внутренние: IN16 — датчик температуры кристалла, IN17 — Vrefint.', MUT, 'start', 12)
a('</svg>')

svg = os.path.join(D, 'pinout_adc_free.svg')
open(svg, 'w', encoding='utf-8').write('\n'.join(S))
wrap = os.path.join(D, '_pinout_wrap.html')
open(wrap, 'w', encoding='utf-8').write('<html><body style="margin:0"><img src="pinout_adc_free.svg" width="%d" height="%d"></body></html>' % (W, H))
edge = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
png = os.path.join(D, 'pinout_adc_free.png')
subprocess.run([edge, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=2',
                '--user-data-dir=' + os.path.join(os.environ['TEMP'], 'v307_edge'), '--window-size=%d,%d' % (W, H),
                '--screenshot=' + png, 'file:///' + wrap.replace('\\', '/')], capture_output=True)
os.remove(wrap)
print(svg, png, os.path.getsize(png))
