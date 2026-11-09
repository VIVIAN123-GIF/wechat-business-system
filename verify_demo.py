# -*- coding: utf-8 -*-
# 只读验证脚本：检查 seed_demo.sql 是否成功写入
import sqlite3, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

c = sqlite3.connect('ms.db')
c.row_factory = sqlite3.Row

print('--- 运动商品 ---')
rows = c.execute("SELECT id, name, price, stock FROM products WHERE category='运动户外' ORDER BY id").fetchall()
for r in rows:
    print(f"  id={r['id']:<3} {r['name']:<20} {r['price']/100:.2f}元  库存{r['stock']}")
print(f'  共 {len(rows)} 个')

print('\n--- 购物车 ---')
rows = c.execute("""
    SELECT u.username, p.name, ci.quantity
    FROM cart_items ci
    JOIN users u ON u.id=ci.user_id
    JOIN products p ON p.id=ci.product_id
""").fetchall()
for r in rows:
    print(f"  {r['username']:<8} {r['name']:<20} x{r['quantity']}")
print(f'  共 {len(rows)} 条')

print('\n--- 订单状态分布 ---')
status_name = {0:'待支付',1:'已支付',2:'已发货',3:'已完成',4:'已取消',5:'退货中'}
rows = c.execute("SELECT status, COUNT(*) n FROM orders GROUP BY status ORDER BY status").fetchall()
for r in rows:
    print(f"  status={r['status']} ({status_name.get(r['status'],'?')})  数量 {r['n']}")

print('\n--- 退货单 ---')
rows = c.execute("SELECT id, status, reason FROM returns ORDER BY id").fetchall()
st = {0:'待审核',1:'已通过',2:'已拒绝',3:'已完成退款'}
for r in rows:
    print(f"  退货单#{r['id']:<3} {st.get(r['status'],'?'):<8} 原因: {r['reason']}")

print('\n--- 促销活动 ---')
rows = c.execute("SELECT id, name, type FROM promotions ORDER BY id").fetchall()
for r in rows:
    print(f"  id={r['id']:<3} {r['name']}")
