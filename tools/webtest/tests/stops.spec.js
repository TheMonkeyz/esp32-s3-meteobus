// My stops (the buses, from esp32-s3-rtcquebec): add a favourite (stop + route -> directions -> add), reorder, remove;
// the RTC's refusals are explained. The display asks the RTC (bus_routes.c): the page never does.
const { test, expect, state } = require('./fixtures');

async function find(page, stop, route) {
  await page.locator('#favStop').fill(stop);
  await page.locator('#favRoute').fill(route);
  await page.locator('#favFind').click();
}
const posts = async (request, url) => (await state(request)).log.filter(l => l.method === 'POST' && l.url === url);

test('add a stop: its directions come from the display, the list is saved', async ({ page, request }) => {
  await page.goto('/');
  await expect(page.locator('#favNone')).toBeVisible();
  await find(page, '1025', '800');
  await expect(page.locator('#favDir option')).toHaveText(['Colline Parlementaire', 'Terminus Chute-Montmorency']);
  await page.locator('#favAdd').click();
  await expect(page.locator('#favList .fav')).toHaveCount(1);
  await expect(page.locator('#favList .fav')).toContainText('800');
  await expect(page.locator('#favList .fav')).toContainText('St-Dominique · 1025');
  await expect(page.locator('#favNone')).toBeHidden();
  expect((await state(request)).favs).toEqual([{ stop: '1025', route: '800', dir: '0' }]);
  await expect(page.locator('#favStop')).toHaveValue('');           // ready for the next one
  await expect(page.locator('#favDirBox')).toBeHidden();
});

test('reorder and remove', async ({ page, request }) => {
  await page.goto('/');
  for (const [stop, dir] of [['1025', 0], ['1026', 1]]) {
    await find(page, stop, '800');
    await page.locator('#favDir').selectOption(String(dir));
    await page.locator('#favAdd').click();
    await expect(page.locator('#favStop')).toHaveValue('');
  }
  await expect(page.locator('#favList .fav')).toHaveCount(2);
  // A slow phone: the display has saved the new order, but its answer reaches the page 1.5 s later. The rows on screen
  // are still the old order meanwhile, and a Remove tapped then removed the wrong stop (CI's Mac runner hit this
  // window once, 2026-10-10: the display was left with ['1026']). The page now disables the rows during a save.
  await page.route('**/api/favs', async route => {
    if (route.request().method() !== 'POST') return route.continue();
    const response = await route.fetch();
    await new Promise(r => setTimeout(r, 1500));
    await route.fulfill({ response });
  });
  await page.locator('#favList .fav').nth(1).getByTitle('Move up').click();
  await expect.poll(async () => (await state(request)).favs.map(f => f.stop)).toEqual(['1026', '1025']);
  await page.locator('#favList .fav').nth(0).getByTitle('Remove').click();
  await expect.poll(async () => (await state(request)).favs.map(f => f.stop)).toEqual(['1025']);
  await expect(page.locator('#favList .fav')).toHaveCount(1);
});

test('refusals: unknown route, stop not served, RTC down, bad input, duplicate', async ({ page, request }) => {
  await page.goto('/');
  await find(page, '1025', '4242');
  await expect(page.locator('#favMsg')).toHaveText("Route 4242 doesn't exist.");
  await expect(page.locator('#favDirBox')).toBeHidden();

  await find(page, '9999', '800');                                   // route found; the stop isn't on it
  await page.locator('#favAdd').click();
  await expect(page.locator('#favMsg')).toHaveText("Route 800 doesn't stop at 9999 in that direction.");
  expect((await state(request)).favs).toEqual([]);

  await find(page, '1025', '999');
  await expect(page.locator('#favMsg')).toHaveText("The RTC didn't answer. Try again in a moment.");

  const before = (await posts(request, '/api/route')).length;
  await find(page, '10&25', '800');
  await expect(page.locator('#favMsg')).toHaveText('Check the stop and route numbers.');
  expect((await posts(request, '/api/route')).length, 'nothing asked for bad input').toBe(before);

  await find(page, '1025', '800');
  await page.locator('#favAdd').click();
  await expect(page.locator('#favList .fav')).toHaveCount(1);
  await find(page, '1025', '800');
  await page.locator('#favAdd').click();
  await expect(page.locator('#favMsg')).toHaveText('That stop is already in the list.');
});

test('in French', async ({ page, request }) => {
  await request.post('/api/units', { data: { lang: 'fr' } });
  await page.goto('/');
  await expect(page.locator('#stopsCard h2')).toHaveText('Mes arrêts');
  await find(page, '9999', '800');
  await page.locator('#favAdd').click();
  await expect(page.locator('#favMsg')).toHaveText('Le parcours 800 ne s\'arrête pas au 9999 dans cette direction.');
});

test('on the setup network (no key): a stop can be added', async ({ page, request }) => {
  // rtcquebec, 2026-10-06: a phone that joined the setup network through Wi-Fi setup got 403 on "Find directions"
  await request.post('/__key', { data: { key: '0123456789abcdef' } });
  await request.post('/__setup');
  await page.goto('/');
  await find(page, '1025', '800');
  await expect(page.locator('#favDir option')).toHaveCount(2);
  await page.locator('#favAdd').click();
  await expect(page.locator('#favList .fav')).toHaveCount(1);
  expect((await state(request)).favs).toEqual([{ stop: '1025', route: '800', dir: '0' }]);
});
