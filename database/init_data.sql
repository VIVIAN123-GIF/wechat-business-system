-- ============================================================
-- 微商系统 初始测试数据
-- ============================================================

-- 用户密码为 SHA-256 摘要；演示账号使用非默认强口令。
INSERT INTO users (username, password, real_name, phone, level, role) VALUES
('admin',  '73f8b74dabf09b8a20de224389821f72bcb7682c10cf9be567b90523376924b1', '管理员', '13800000001', 1,  'admin'),
('user1',  '7d6826579572e737e7786df480660bd4ed20736575f20c437582c47639dbef86', '测试用户一', '13800000002', 1, 'customer'),
('user2',  '28559c76d6c2a13c45eeff87c2fb980d6e37660be97fd09b8ca55f0073f40d8e', '测试用户二', '13800000003', 1, 'customer');

-- 商品（价格单位：分；含"限量款"库存=1，用于演示互斥防超卖）
INSERT INTO products (name, price, stock, description, category) VALUES
('高端护肤品套装', 29900, 15,  '深层补水保湿，明星单品组合', '美妆'),
('有机茶叶礼盒',   18800, 32,  '高山有机绿茶，送礼佳品',   '食品'),
('手工皮具钱包',   39900, 8,   '头层牛皮，手工缝制',       '箱包'),
('降噪无线耳机',   89900, 5,   '主动降噪，续航30小时',     '数码'),
('智能手环',       24900, 20,  '心率血氧监测，50米防水',   '数码'),
('限量款背包',     129900, 1,  '仅此一件，售完即止',       '箱包');

-- 促销活动
-- type=1 打折 param1=折扣率(0.9=9折)
-- type=2 降价 param1=直降金额
-- type=3 满减券 param1=满额门槛 param2=减免金额
-- type=4 阶梯折扣 param1/param2=第1阶梯(件数,折扣率) param3/param4=第2阶梯 param5/param6=第3阶梯
INSERT INTO promotions (name, type, param1, param2, param3, param4, param5, param6, status, exclusive_group) VALUES
('全场9折',      1, 0.90, NULL, NULL, NULL, NULL, NULL, 1, 'order_discount'),
('满200减30',    3, 200,  30,   NULL, NULL, NULL, NULL, 1, ''),
('满500减100',   3, 500,  100,  NULL, NULL, NULL, NULL, 1, 'order_discount'),
('数码直降50',   2, 50,   NULL, NULL, NULL, NULL, NULL, 1, ''),
('第2件9折',     4, 1,    1.0,  2,    0.9,  NULL, NULL, 1, ''),
('第3件免单',    4, 2,    1.0,  3,    0.0,  NULL, NULL, 1, '');
