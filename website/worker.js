export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname.endsWith("/")) url.pathname += "index.html";
    return env.ASSETS.fetch(new Request(url, request));
  },
};
