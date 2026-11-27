const bus = new EventTarget();

export function tell(name, detail) {
  bus.dispatchEvent(new CustomEvent(name, { detail }));
}

export function hear(name, listener) {
  const heard = (event) => listener(event.detail);
  bus.addEventListener(name, heard);
  return () => bus.removeEventListener(name, heard);
}
