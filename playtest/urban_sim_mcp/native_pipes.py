"""Paid, paced native water/sewer trenches following an existing road path."""
from .street_layout import runs


async def lay_road_pipes(session,player,path):
    before=session.last_state['funds']
    for a,b in runs(path):
        cells=(await session.request('inspect',{'section':'tiles','x':min(a[0],b[0]),
            'y':min(a[1],b[1]),'width':abs(a[0]-b[0])+1,'height':abs(a[1]-b[1])+1,'limit':512}))['items']
        if all(t['utility']['waterPipe'] and t['utility']['sewerPipe'] for t in cells):continue
        if not session.last_state['tools']['pipe']:await player.key('X')
        for _ in range(5):
            if session.last_state['tools']['pipe_kind']==3:break
            await player.key('X')
        if session.last_state['tools']['pipe_remove']:await player.key('V')
        if not session.last_state['tools']['pipe_snap']:await player.key('O')
        await player.frame_tile((a[0]+b[0])//2,(a[1]+b[1])//2)
        points=[]
        for tile in (a,b):
            p=await session.request('coordinates',{'direction':'tile_to_screen','x':tile[0],'y':tile[1]})
            points.append((p['x'],p['y']))
        quote={}
        cash=session.last_state['funds']
        async def preview():
            quote.update(session.last_state['preview']['pipe'])
            assert quote['valid'],quote
        await player.drag(*points,on_hold=preview)
        assert cash-session.last_state['funds']==quote['cost']
    # Verify every routing tile, including bends and shared junctions.
    for x,y in path:
        tile=(await session.request('inspect',{'section':'tiles','x':x,'y':y,'width':1,'height':1,'limit':1}))['items'][0]
        assert tile['utility']['waterPipe'] and tile['utility']['sewerPipe'],(x,y)
    await player.key('Escape')
    return {'cost':before-session.last_state['funds'],'routing_tiles':len(path),'continuous':True}
