-- ============================================================
-- 演示数据补充脚本：体育运动商品 + 购物车 + 促销 + 订单 + 退货
-- 用法：先关闭 server.exe，再用 python 执行本文件。
-- 价格单位：分（39900 = 399.00 元）
-- ============================================================

-- ========== 1. 体育运动类商品 ==========
INSERT INTO products (name, price, stock, description, category, status) VALUES
('专业跑步鞋男款缓震',   39900, 20, '轻量透气鞋面，回弹中底，适合长距离慢跑', '运动户外', 1),
('加厚防滑瑜伽垫',        8900, 50, '8mm加厚NBR材质，双面防滑，含绑带',       '运动户外', 1),
('速干透气运动T恤',       7900, 40, '吸湿速干面料，跑步/健身通用',           '运动户外', 1),
('可调节哑铃套装20kg',   29900, 10, '一对共20kg，可拆卸配重，家用健身',       '运动户外', 1),
('7号标准篮球',          12900, 15, 'PU耐磨表皮，室内外通用，成人比赛款',     '运动户外', 1),
('智能运动手表',         59900,  8, '心率血氧监测，50米防水，GPS轨迹',       '运动户外', 1),
('便携筋膜枪',           34900, 12, '4档力度，6个按摩头，静音电机',           '运动户外', 1),
('露营折叠椅',           15900, 18, '轻量铝合金支架，承重120kg，含收纳袋',    '运动户外', 1);

-- ========== 2. 新增促销（参数格式与现有"满200减30"一致） ==========
INSERT INTO promotions (name, type, param1, param2, param3, param4, status) VALUES
('运动户外满300减50', 3, 300,  50,  NULL, NULL, 1),
('运动装备第2件半价', 4, 1,    1.0, 2,    0.5,  1);

-- ========== 3. 购物车：user1(2) 和 user2(3) 各塞几样 ==========
INSERT INTO cart_items (user_id, product_id, sku_id, quantity) VALUES
(2, (SELECT id FROM products WHERE name='专业跑步鞋男款缓震'), 0, 1),
(2, (SELECT id FROM products WHERE name='加厚防滑瑜伽垫'),     0, 1),
(2, (SELECT id FROM products WHERE name='速干透气运动T恤'),    0, 2),
(3, (SELECT id FROM products WHERE name='7号标准篮球'),        0, 1),
(3, (SELECT id FROM products WHERE name='可调节哑铃套装20kg'), 0, 1);

-- ========== 4. 补各状态订单 ==========
-- 订单状态：0待支付 1已支付 2已发货 3已完成 4已取消 5退货中
INSERT INTO orders (order_no, user_id, total_amount, discount_amount, final_amount, status,
                    pay_time, address_receiver, address_phone, address_detail, create_time) VALUES
('MS20261109201001001', 2, 55700, 0,    55700, 0, NULL,                  '张薇', '13800000002', '吉林大学前卫南区',     '2026-11-08 20:10:00'),
('MS20261109193002002', 2, 47800, 0,    47800, 2, '2026-11-07 10:00:00', '张薇', '13800000002', '吉林大学前卫南区',     '2026-11-06 19:30:00'),
('MS20261109152003003', 2, 17800, 2000, 15800, 3, '2026-11-03 21:00:00', '张薇', '13800000002', '吉林大学前卫南区',     '2026-11-01 15:20:00'),
('MS20261109180004004', 2, 59900, 0,    59900, 5, '2026-11-05 12:00:00', '张薇', '13800000002', '吉林大学前卫南区',     '2026-11-04 18:00:00'),
('MS20261109221005005', 3, 31800, 3000, 28800, 3, '2026-11-02 09:00:00', '测试二', '13800000003', '长春市朝阳区',       '2026-10-30 22:10:00'),
('MS20261109205506006', 3, 29900, 0,    29900, 1, '2026-11-08 21:00:00', '测试二', '13800000003', '长春市朝阳区',       '2026-11-08 20:55:00');

-- 订单明细（快照：product_name / price 写死）
INSERT INTO order_items (order_id, product_id, product_name, price, quantity, sku_label) VALUES
((SELECT id FROM orders WHERE order_no='MS20261109201001001'),
 (SELECT id FROM products WHERE name='专业跑步鞋男款缓震'), '专业跑步鞋男款缓震', 39900, 1, ''),
((SELECT id FROM orders WHERE order_no='MS20261109201001001'),
 (SELECT id FROM products WHERE name='速干透气运动T恤'),    '速干透气运动T恤',     7900, 2, ''),
((SELECT id FROM orders WHERE order_no='MS20261109193002002'),
 (SELECT id FROM products WHERE name='7号标准篮球'),        '7号标准篮球',        12900, 1, ''),
((SELECT id FROM orders WHERE order_no='MS20261109193002002'),
 (SELECT id FROM products WHERE name='便携筋膜枪'),         '便携筋膜枪',         34900, 1, ''),
((SELECT id FROM orders WHERE order_no='MS20261109152003003'),
 (SELECT id FROM products WHERE name='加厚防滑瑜伽垫'),     '加厚防滑瑜伽垫',      8900, 2, ''),
((SELECT id FROM orders WHERE order_no='MS20261109180004004'),
 (SELECT id FROM products WHERE name='智能运动手表'),       '智能运动手表',       59900, 1, ''),
((SELECT id FROM orders WHERE order_no='MS20261109221005005'),
 (SELECT id FROM products WHERE name='露营折叠椅'),         '露营折叠椅',         15900, 2, ''),
((SELECT id FROM orders WHERE order_no='MS20261109205506006'),
 (SELECT id FROM products WHERE name='可调节哑铃套装20kg'), '可调节哑铃套装20kg', 29900, 1, '');

-- ========== 5. 售后退货（待审核 / 已拒绝 / 已完成退款） ==========
-- 退货状态：0待审核 1审核通过 2审核拒绝 3已完成；orig_status = 申请时订单原状态
INSERT INTO returns (order_id, user_id, reason, status, orig_status, apply_time, audit_time) VALUES
((SELECT id FROM orders WHERE order_no='MS20261109180004004'), 2,
 '手表续航与商品描述不符，申请退货', 0, 1, '2026-11-08 19:00:00', NULL),
((SELECT id FROM orders WHERE order_no='MS20261109152003003'), 2,
 '想换个颜色，不影响二次销售',       2, 3, '2026-11-04 10:00:00', '2026-11-04 14:00:00'),
((SELECT id FROM orders WHERE order_no='MS20261109221005005'), 3,
 '椅子收到有划痕，质量问题',         3, 3, '2026-11-03 11:00:00', '2026-11-03 16:00:00');
