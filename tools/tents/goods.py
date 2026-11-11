def room_glimpse(h, w, bright):
    """Beedle's shop as seen through the parting in the curtain: shelves of potion bottles, bombs,
    a jug of lantern oil, a quiver of arrows and a wooden shield on the floor, herbs hanging from
    above. Coordinates are in game units on the plane (83 wide, 230 tall), y up from the floor."""
    SS = 4
    H, W = h * SS, w * SS
    PW, PH = GAP_W * 3.2, 230.0
    def X(x): return (x / PW + 0.5) * W            # x: -PW/2 .. PW/2
    def Y(y): return (1 - y / PH) * H              # y: 0 floor .. PH
    sc = W / PW                                    # pixels per unit
    img = np.zeros((H, W, 3), np.float32)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # back wall: lamplit canvas, brightest round the middle
    wall = np.array([168, 120, 72], np.float32)
    lamp = np.exp(-((xx - W * 0.5) / (W * 0.55)) ** 2 - ((yy - Y(150)) / (H * 0.45)) ** 2)
    img[:] = wall * (0.45 + 0.6 * lamp)[..., None]
    img *= (0.94 + 0.06 * np.sin(xx / sc * 0.9))[..., None]          # canvas seams
    floor = yy > Y(14)
    img[floor] = np.array([92, 64, 40], np.float32) * (0.7 + 0.3 * (yy[floor] / H))[..., None]

    def shade_mask(mask, color, cx, cy, rx, ry, gloss=0.0):
        """Round-ish shading inside mask: lit from upper left, rim darker, optional glossy spot."""
        nx = (xx - cx) / max(rx, 1); ny = (yy - cy) / max(ry, 1)
        lit = np.clip(1.05 - 0.55 * np.hypot(nx + 0.35, ny + 0.4), 0.25, 1.1)
        c = np.array(color, np.float32) * lit[..., None]
        if gloss:
            spot = np.exp(-(((nx + 0.4) / 0.18) ** 2 + ((ny + 0.45) / 0.25) ** 2)) * gloss
            c = c + (255 - c) * spot[..., None]
        img[mask] = c[mask]
    def ell(cx, cy, rx, ry):
        return ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1
    def rect(x0, y0, x1, y1):
        return (xx >= min(x0, x1)) & (xx <= max(x0, x1)) & (yy >= min(y0, y1)) & (yy <= max(y0, y1))

    # shelves (two boards on brackets)
    for sy in (128.0, 80.0):
        board = rect(X(-PW / 2), Y(sy), X(PW / 2), Y(sy - 3.2))
        img[board] = np.array([118, 80, 46], np.float32) * (0.8 + 0.2 * np.sin(xx[board] / sc * 3))[..., None]
        img[rect(X(-PW / 2), Y(sy - 3.2), X(PW / 2), Y(sy - 5))] *= 0.55     # shadow under the board
        for bx in (-28.0, 28.0):
            img[rect(X(bx - 1.2), Y(sy - 3), X(bx + 1.2), Y(sy - 12))] = np.array([96, 64, 36], np.float32)

    # top shelf: potion bottles (round body, neck, cork), red, green, blue, empty, red
    def bottle(x, base, liquid, fill=0.7):
        r = 4.2; cy = base + r
        body = ell(X(x), Y(cy), r * sc, r * sc * 1.05)
        neck = rect(X(x - 1.4), Y(cy + r * 0.8), X(x + 1.4), Y(cy + r + 4.5))
        cork = rect(X(x - 1.7), Y(cy + r + 4.5), X(x + 1.7), Y(cy + r + 6.5))
        glass = np.array([150, 170, 160], np.float32)
        shade_mask(body | neck, glass * 0.55 + 40, X(x), Y(cy), r * sc, r * sc, gloss=0.0)
        if liquid is not None:
            level = Y(base + 2 * r * fill)
            shade_mask(body & (yy >= level), liquid, X(x), Y(cy), r * sc, r * sc)
        shade_mask(cork, (150, 108, 64), X(x), Y(cy + r + 5.5), 1.7 * sc, 1 * sc)
        spot = ell(X(x - r * 0.4), Y(cy + r * 0.35), r * 0.22 * sc, r * 0.4 * sc) & body
        img[spot] = img[spot] * 0.4 + 255 * 0.6
    for x, liq in ((-30, (200, 40, 36)), (-19, (60, 170, 70)), (-8, (60, 110, 210)), (3, None),
                   (14, (200, 40, 36)), (25, (60, 170, 70))):
        bottle(x, 128.0, liq)

    # lower shelf: three bombs with fuses, and a jug of lantern oil
    for k, x in enumerate((-31.0, -22.0, -13.0)):
        r = 4.3; cy = 80.0 + r
        b = ell(X(x), Y(cy), r * sc, r * sc)
        shade_mask(b, (44, 44, 54), X(x), Y(cy), r * sc, r * sc, gloss=0.5)
        cap = rect(X(x - 1.3), Y(cy + r - 0.5), X(x + 1.3), Y(cy + r + 1.4))
        img[cap] = np.array([120, 110, 96], np.float32)
        fx = np.linspace(0, 1, 30)
        for f in fx:
            px, py = X(x + 0.6 + 2.5 * f + 0.6 * math.sin(f * 6 + k)), Y(cy + r + 1.4 + 3.5 * f)
            img[ell(px, py, 0.45 * sc, 0.45 * sc)] = np.array([196, 170, 120], np.float32)
    jx = 10.0
    jug = ell(X(jx), Y(80 + 7), 7.5 * sc, 7 * sc) & (yy <= Y(80))
    jug |= ell(X(jx), Y(80 + 7), 7.5 * sc, 7 * sc)
    shade_mask(jug, (170, 112, 54), X(jx), Y(87), 7.5 * sc, 7 * sc, gloss=0.25)
    img[rect(X(jx - 2), Y(94), X(jx + 2), Y(99))] = np.array([150, 98, 48], np.float32)
    img[rect(X(jx - 2.6), Y(99), X(jx + 2.6), Y(101))] = np.array([120, 84, 52], np.float32)
    # a little Hylian price tag on the jug
    img[rect(X(jx + 3), Y(90), X(jx + 8), Y(86))] = np.array([230, 216, 180], np.float32)
    # a stack of folded cloth at the end of the shelf
    for k, col in enumerate(((60, 110, 112), (140, 50, 48), (210, 196, 160))):
        img[rect(X(22), Y(80 + k * 2.6), X(36), Y(80 + (k + 1) * 2.6 - 0.4))] = np.array(col, np.float32)

    # floor: wooden crate, a quiver of arrows leaning on it, a round wooden shield leaning on the right
    crate = rect(X(-36), Y(0), X(-8), Y(26))
    img[crate] = np.array([128, 88, 52], np.float32) * (0.75 + 0.25 * ((yy[crate] / sc) % 6 > 0.8))[..., None]
    for x in (-36, -8):
        img[rect(X(x), Y(0), X(x + 2.4 * (1 if x < 0 else -1)), Y(26))] *= 0.7
    # quiver: tilted leather tube with fletchings sticking out
    ang = math.radians(16); qx, qy = -4.0, 0.0
    def along(t, s):   # t along the quiver (0 bottom .. 1 top), s across (-1..1)
        return (qx + math.sin(ang) * 34 * t + math.cos(ang) * 4.2 * s, qy + math.cos(ang) * 34 * t - math.sin(ang) * 4.2 * s)
    poly = np.array([[X(along(0, -1)[0]), Y(along(0, -1)[1])], [X(along(1, -1)[0]), Y(along(1, -1)[1])],
                     [X(along(1, 1)[0]), Y(along(1, 1)[1])], [X(along(0, 1)[0]), Y(along(0, 1)[1])]], np.int32)
    qm = np.zeros((H, W), np.uint8); cv2.fillPoly(qm, [poly], 1); qm = qm.astype(bool)
    shade_mask(qm, (110, 62, 36), X(along(0.5, 0)[0]), Y(along(0.5, 0)[1]), 5 * sc, 20 * sc)
    for k, s_ in enumerate((-0.6, 0.0, 0.6)):
        bx, by = along(1.0, s_)
        tx, ty = bx + math.sin(ang) * 9, by + math.cos(ang) * 9
        cv2.line(img, (int(X(bx)), int(Y(by))), (int(X(tx)), int(Y(ty))), (150, 120, 80), int(0.8 * sc))
        fl = np.array([[X(tx), Y(ty)], [X(tx - 2.2), Y(ty - 5)], [X(tx + 2.2), Y(ty - 5)]], np.int32)
        cv2.fillPoly(img, [fl], (220, 220, 210) if k != 1 else (200, 60, 50))
    # wooden shield: rounded top, iron rim and boss
    sx, sw, sh = 26.0, 22.0, 30.0
    top = ell(X(sx), Y(sh - sw / 2), sw / 2 * sc, sw / 2 * sc) & (yy <= Y(sh - sw / 2))
    lower = rect(X(sx - sw / 2), Y(sh - sw / 2), X(sx + sw / 2), Y(4)) & ell(X(sx), Y(sh - sw / 2), sw / 2 * sc, (sh - 4) * sc * 1.1)
    shield = top | lower
    shade_mask(shield, (150, 100, 56), X(sx), Y(sh / 2), sw / 2 * sc, sh / 2 * sc)
    inner_ = (cv2.erode(shield.astype(np.uint8), np.ones((int(2 * sc), int(2 * sc)), np.uint8)) > 0)
    img[shield & ~inner_] = np.array([110, 112, 118], np.float32)
    for k in range(1, 4):   # planks
        img[shield & inner_ & (np.abs(xx - X(sx - sw / 2 + sw * k / 4)) < 0.25 * sc)] *= 0.6
    boss = ell(X(sx), Y(sh * 0.48), 3 * sc, 3 * sc)
    shade_mask(boss, (130, 132, 140), X(sx), Y(sh * 0.48), 3 * sc, 3 * sc, gloss=0.5)

    # hanging from above: bundles of dried herbs and a string of red peppers
    for k, (x, ln) in enumerate(((-24, 26), (-6, 18), (12, 30), (28, 22))):
        cv2.line(img, (int(X(x)), 0), (int(X(x)), int(Y(PH - ln))), (90, 70, 50), int(0.5 * sc))
        if k == 2:
            for m in range(7):
                cy_ = PH - ln - m * 4.2
                shade_mask(ell(X(x + (m % 2) * 1.2 - 0.6), Y(cy_), 1.8 * sc, 2.6 * sc), (190, 40, 30), X(x), Y(cy_), 2 * sc, 2.6 * sc, gloss=0.4)
        else:
            for m in range(9):
                a = math.radians(-35 + m * 9)
                x1, y1 = X(x + math.sin(a) * 10), Y(PH - ln - math.cos(a) * 14)
                cv2.line(img, (int(X(x)), int(Y(PH - ln))), (int(x1), int(y1)), (88 + m * 4, 120 - m * 2, 60), int(1.1 * sc))

    # a warm haze from the lamp, then down to the region size
    img = cv2.GaussianBlur(img, (0, 0), 0.6 * SS)
    img = cv2.resize(img, (w, h), interpolation=cv2.INTER_AREA)
    if bright:
        yy2, xx2 = np.mgrid[0:h, 0:w].astype(np.float32)
        haze = np.exp(-((xx2 - w * 0.5) / (w * 0.6)) ** 2 - ((yy2 - h * 0.35) / (h * 0.5)) ** 2)
        img = img * 1.25 + np.array([60, 36, 10], np.float32) * haze[..., None]
    else:
        img = img * 0.55
    return np.clip(img, 0, 255)

