# 博客使用指南

## 目录
- [1. 文章管理](#1-文章管理)
  - [1.1 创建新文章](#11-创建新文章)
  - [1.2 文章配置说明](#12-文章配置说明)
  - [1.3 文章编写规范](#13-文章编写规范)
- [2. 分类管理](#2-分类管理)
  - [2.1 创建新分类](#21-创建新分类)
  - [2.2 分类页面定制](#22-分类页面定制)
- [3. 图片管理](#3-图片管理)
- [4. 站点更新](#4-站点更新)
- [5. 主题定制](#5-主题定制)

## 1. 文章管理

### 1.1 创建新文章

1. 在 `_posts` 文件夹中创建新的 Markdown 文件
2. 文件命名规则：`YYYY-MM-DD-title.md`
   - 例如：`2025-10-17-my-first-post.md`
   - 日期必须采用 `YYYY-MM-DD` 格式
   - 标题使用小写字母，单词间用连字符 `-` 连接

### 1.2 文章配置说明

每篇文章开头需要包含 YAML 头信息：

```yaml
---
layout: post
title: "文章标题"
date: YYYY-MM-DD HH:MM:SS +0800
category: 分类名称
tags: [标签1, 标签2]
excerpt: 文章摘要，会显示在首页和分类页面
featured: true  # 可选，设为 true 会在首页突出显示
image: /images/posts/example.jpg  # 可选，文章配图
---
```

配置项说明：
- `layout`: 固定为 `post`
- `title`: 文章标题，使用引号包围
- `date`: 发布时间，格式为 `YYYY-MM-DD HH:MM:SS +0800`
- `category`: 文章分类，必须是已存在的分类
- `tags`: 文章标签，可选，用方括号包围
- `excerpt`: 文章摘要，建议 50-100 字
- `featured`: 是否在首页突出显示
- `image`: 文章配图路径，相对于站点根目录

### 1.3 文章编写规范

1. 正文使用 Markdown 格式
2. 支持的特殊功能：
   - 数学公式（使用 `$` 和 `$$` 包围）
   - 代码高亮（使用三个反引号包围）
   - 图片引用（使用相对路径）

示例：

````markdown
## 标题

正文内容...

### 数学公式
行内公式：$E=mc^2$
单独一行：
$$
\sum_{i=1}^n i = \frac{n(n+1)}{2}
$$

### 代码示例
```python
def hello_world():
    print("Hello, World!")
```

### 图片
![图片描述](/images/example.jpg)
````

## 2. 分类管理

### 2.1 创建新分类

1. 在 `categories` 文件夹中创建新的 HTML 文件
   - 文件名使用小写字母，例如：`python.html`

2. 添加以下内容：

```yaml
---
layout: category
title: 分类名称
category: category-name
---
```

3. 在 `categories/index.html` 中添加新分类的链接

### 2.2 分类页面定制

分类页面支持以下自定义选项：
- 背景图片
- 描述文字
- 排序方式

示例配置：

```yaml
---
layout: category
title: Python 编程
category: python
description: Python相关的教程和经验分享
header:
  overlay_image: /images/categories/python-bg.jpg
  overlay_filter: 0.5
sort_by: date  # 可选：date, title
sort_order: desc  # 可选：asc, desc
---
```

## 3. 图片管理

1. 所有图片存放在 `images` 文件夹中
2. 建议的文件夹结构：
   - `images/posts/`: 文章配图
   - `images/categories/`: 分类页面背景
   - `images/common/`: 通用图片资源

图片引用示例：
```markdown
![图片描述]({{ site.baseurl }}/images/posts/example.jpg)
```

## 4. 站点更新

完成修改后，按以下步骤更新站点：

1. 本地预览：
```bash
bundle exec jekyll serve
```

2. 提交更改：
```bash
git add .
git commit -m "更新说明"
git push
```

3. 等待 GitHub Pages 自动构建（通常需要 1-5 分钟）

## 5. 主题定制

本站使用了自定义的玻璃态主题，主要样式文件：

- `public/css/glass.css`: 玻璃态效果
- `public/css/timeline.css`: 时间轴样式
- `public/css/lanyon.css`: 基础布局
- `public/css/sidebar-button.css`: 侧边栏按钮

### 颜色主题

主要的颜色变量定义在样式文件中，可以通过修改这些变量来更改整体配色：

```css
:root {
  --primary-color: #3498db;
  --secondary-color: #2c3e50;
  --background-color: rgba(255, 255, 255, 0.8);
  --text-color: #1f1f1f;
  --link-color: #3498db;
}
```

## 提示与技巧

1. 文章预览
   - 使用 `<!--more-->` 标记来控制首页摘要长度
   - 或者在 YAML 头信息中使用 `excerpt` 字段

2. 图片优化
   - 大图建议压缩后再上传
   - 考虑使用 WebP 格式
   - 建议图片宽度不超过 1920px

3. 移动端适配
   - 测试不同设备上的显示效果
   - 图片使用响应式布局
   - 控制段落长度

4. SEO 优化
   - 为每篇文章添加合适的描述
   - 使用有意义的文件名
   - 添加适当的标签

## 常见问题

1. 文章不显示
   - 检查文件名格式
   - 检查 YAML 头信息格式
   - 确认分类名称正确

2. 图片无法显示
   - 检查图片路径是否正确
   - 确认图片已提交到仓库
   - 检查图片格式是否支持

3. 本地预览与线上不一致
   - 清理缓存：`bundle exec jekyll clean`
   - 检查配置文件是否有语法错误
   - 确认所有依赖都已安装