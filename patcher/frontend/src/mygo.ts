export interface MyGoWindowControls {
  minimize(): Promise<void>;
  maximize(): Promise<void>;
  unmaximize(): Promise<void>;
  toggleMaximize(): Promise<void>;
  isMaximized(): Promise<boolean>;
  toggleFullScreen(): Promise<void>;
  close(): Promise<void>;
  setTitle(title: string): Promise<void>;
}

export interface MyGoRuntime {
  call<T = unknown>(method: string, ...args: unknown[]): Promise<T>;
  on<T = unknown>(event: string, listener: (payload: T) => void): () => void;
  readonly window: MyGoWindowControls;
}

declare global {
  interface Window {
    mygo?: MyGoRuntime;
  }
}

export function callMethod<T = unknown>(method: string, ...args: unknown[]): Promise<T> {
  const rt = window.mygo;
  if (!rt) {
    return Promise.reject(new Error("MyGo 运行时未就绪"));
  }
  return rt.call<T>(method, ...args);
}

export function onEvent<T = unknown>(name: string, listener: (payload: T) => void): () => void {
  const rt = window.mygo;
  if (!rt) {
    return () => {};
  }
  return rt.on<T>(name, listener);
}

export function setNativeWindowTitle(title: string): Promise<void> {
  const rt = window.mygo;
  if (!rt) {
    return Promise.resolve();
  }
  return rt.window.setTitle(title);
}
