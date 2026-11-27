const SAMPLE = 8;
const OPAQUE = 128;
const CHANNELS = 4;
const GLOW = { lightness: 27, boost: 1.4, most: 0.45 };
const WASH = { lightness: 19, edge: 13, boost: 1.3, most: 0.42, edge_share: 0.7 };
const NEUTRAL = { hue: 0, saturation: 0 };

function sampled(image) {
  if (image?.data) return { data: image.data, width: image.width, height: image.height };
  const canvas = new OffscreenCanvas(SAMPLE, SAMPLE);
  const context = canvas.getContext("2d", { willReadFrequently: true });
  context.drawImage(image, 0, 0, SAMPLE, SAMPLE);
  return { data: context.getImageData(0, 0, SAMPLE, SAMPLE).data, width: SAMPLE, height: SAMPLE };
}

export function average_of(image) {
  const { data, width, height } = sampled(image);
  const step = Math.max(Math.floor((width * height) / (SAMPLE * SAMPLE)), 1) * CHANNELS;
  const sum = [0, 0, 0];
  let count = 0;
  for (let at = 0; at + CHANNELS <= data.length; at += step) {
    if (data[at + 3] < OPAQUE) continue;
    sum[0] += data[at];
    sum[1] += data[at + 1];
    sum[2] += data[at + 2];
    count += 1;
  }
  return count ? sum.map((channel) => channel / count / 255) : null;
}

export function hue_of(colors) {
  const weight = colors.reduce((total, color) => total + color.weight, 0);
  if (!weight) return null;
  const [red, green, blue] = [0, 1, 2].map(
    (channel) => colors.reduce((total, color) => total + color.rgb[channel] * color.weight, 0) / weight,
  );
  const high = Math.max(red, green, blue);
  const low = Math.min(red, green, blue);
  const lightness = (high + low) / 2;
  const chroma = high - low;
  if (!chroma) return { hue: 0, saturation: 0 };
  const saturation = chroma / (1 - Math.abs(2 * lightness - 1));
  let hue = high === red ? ((green - blue) / chroma) % 6 : high === green ? (blue - red) / chroma + 2 : (red - green) / chroma + 4;
  hue = (hue * 60 + 360) % 360;
  return { hue: Math.round(hue), saturation };
}

function hsl(hue, saturation, lightness) {
  return `hsl(${hue} ${Math.round(saturation * 100)}% ${lightness}%)`;
}

export function glow_of(tint) {
  const { hue, saturation } = tint ?? NEUTRAL;
  return hsl(hue, Math.min(saturation * GLOW.boost, GLOW.most), GLOW.lightness);
}

export function wash_of(tint) {
  const { hue, saturation } = tint ?? NEUTRAL;
  const strength = Math.min(saturation * WASH.boost, WASH.most);
  return [hsl(hue, strength, WASH.lightness), hsl(hue, strength * WASH.edge_share, WASH.edge)];
}
