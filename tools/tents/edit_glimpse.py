"""Nintendo-style: take the interior screenshot and edit it into the curtain-gap textures.
Day: dimmer and warmer than outdoors, edges in shadow. Night: lamplit glow, bloom round the bright bits."""
import numpy as np, cv2
def load(p): return cv2.cvtColor(cv2.imread(p), cv2.COLOR_BGR2RGB).astype(np.float32)
day, night = load('interior2/view_glimpse_day.png'), load('interior2/view_glimpse_night.png')
h, w = day.shape[:2]
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
u, v = xx / w, yy / h
vign = np.clip(1 - ((np.abs(u - 0.5) / 0.5) ** 2.2) * 0.55 - (np.clip(0.25 - v, 0, 1) / 0.25) * 0.35, 0.35, 1)
def soften(img, s): return cv2.GaussianBlur(img, (0, 0), s)
d = soften(day, 1.2) * np.array([0.62, 0.55, 0.46]) * vign[..., None]
n = soften(night, 1.2) * np.array([1.35, 1.12, 0.82])
glow = soften(np.clip(n - 150, 0, None), 18) * 0.9
haze = np.exp(-((u - 0.45) / 0.55) ** 2 - ((v - 0.45) / 0.55) ** 2)
n = (n + glow + np.array([70, 40, 10], np.float32) * haze[..., None]) * (0.55 + 0.45 * vign[..., None])
for name, img in (('glimpse_day.png', d), ('glimpse_night.png', n)):
    cv2.imwrite(f'interior2/{name}', cv2.cvtColor(np.clip(img, 0, 255).astype(np.uint8), cv2.COLOR_RGB2BGR))
o = np.hstack([np.clip(d, 0, 255), np.clip(n, 0, 255)]).astype(np.uint8)
cv2.imwrite('interior2/glimpse_edited.png', cv2.cvtColor(o, cv2.COLOR_RGB2BGR))
