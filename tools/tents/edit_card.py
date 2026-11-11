"""Grade the backdrop card like the old glimpse: dim and warm by day, lamplit glow at night."""
import numpy as np, cv2
def load(p): return cv2.cvtColor(cv2.imread(p), cv2.COLOR_BGR2RGB).astype(np.float32)
day, night = load('interior2/card_day_raw.png'), load('interior2/card_night_raw.png')
h, w = day.shape[:2]
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32); u, v = xx / w, yy / h
vign = np.clip(1 - ((np.abs(u - 0.5) / 0.5) ** 2.5) * 0.4, 0.5, 1)
soft = lambda img, s: cv2.GaussianBlur(img, (0, 0), s)
d = soft(day, 1.0) * np.array([0.62, 0.55, 0.46]) * vign[..., None]
n = soft(night, 1.0) * np.array([1.35, 1.12, 0.82])
glow = soft(np.clip(n - 150, 0, None), 16) * 0.9
haze = np.exp(-((u - 0.5) / 0.6) ** 2 - ((v - 0.4) / 0.6) ** 2)
n = (n + glow + np.array([70, 40, 10], np.float32) * haze[..., None]) * (0.6 + 0.4 * vign[..., None])
for name, img in (('card_day.png', d), ('card_night.png', n)):
    cv2.imwrite(f'interior2/{name}', cv2.cvtColor(np.clip(img, 0, 255).astype(np.uint8), cv2.COLOR_RGB2BGR))
