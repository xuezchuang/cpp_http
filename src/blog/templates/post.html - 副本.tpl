<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=2">
  <meta name="theme-color" content="#222">
  <meta name="generator" content="Hexo 5.x (compatible)">

  <title>{{TITLE}} | {{SITENAME}}</title>

  <!-- SEO / OpenGraph -->
  <meta name="description" content="{{DESCRIPTION}}">
  <meta property="og:type" content="article">
  <meta property="og:title" content="{{TITLE}}">
  <meta property="og:site_name" content="{{SITENAME}}">
  <meta property="og:description" content="{{DESCRIPTION}}">
  <meta property="og:url" content="{{URL}}">
  <meta property="og:locale" content="zh_CN">
  <meta property="article:author" content="{{AUTHOR_NAME}}">
  <meta property="article:published_time" content="{{DATE_PUBLISHED}}">
  <meta property="article:modified_time" content="{{DATE_UPDATED}}">
  <meta name="twitter:card" content="summary">
  <link rel="canonical" href="{{URL}}">

  <!-- Icons -->
  <link rel="apple-touch-icon" sizes="180x180" href="/images/apple-touch-icon-next.png">
  <link rel="icon" type="image/png" sizes="32x32" href="/images/favicon-32x32-next.png">
  <link rel="icon" type="image/png" sizes="16x16" href="/images/favicon-16x16-next.png">
  <link rel="mask-icon" href="/images/logo.svg" color="#222">

  <!-- NexT assets -->
  <link rel="stylesheet" href="/css/main.css">
  <link rel="stylesheet" href="/lib/font-awesome/css/all.min.css">
  <link rel="stylesheet" href="//cdn.jsdelivr.net/gh/fancyapps/fancybox@3/dist/jquery.fancybox.min.css">

  <!-- NexT config（精简可用） -->
  <script id="hexo-configurations">
    var NexT = window.NexT || {};
    var CONFIG = {
      hostname: "{{HOSTNAME}}",
      root: "/",
      scheme: "Gemini",
      version: "7.8.0",
      exturl: false,
      sidebar: { position: "left", display: "always", padding: 18, offset: 12, onmobile: false },
      copycode: { enable: true, show_result: false, style: null },
      back2top: { enable: true, sidebar: false, scrollpercent: false },
      comments: { style: "tabs", active: null, storage: true, lazyload: false, nav: null },
      fancybox: true,
      motion: { enable: true, async: false, transition: { post_block:"fadeIn", post_header:"slideDownIn", post_body:"slideDownIn", coll_header:"slideLeftIn", sidebar:"slideUpIn" } },
      path: "search.xml"
    };
  </script>

  <noscript>
    <style>
      .use-motion .brand,
      .use-motion .menu-item,
      .sidebar-inner,
      .use-motion .post-block,
      .use-motion .pagination,
      .use-motion .comments,
      .use-motion .post-header,
      .use-motion .post-body,
      .use-motion .collection-header { opacity: initial; }
      .use-motion .site-title, .use-motion .site-subtitle { opacity: initial; top: initial; }
      .use-motion .logo-line-before i { left: initial; }
      .use-motion .logo-line-after i { right: initial; }
    </style>
  </noscript>
</head>

<style>
/* 容器定位 */
pre, figure.highlight { position: relative; }

/* 按钮 */

.next-copy-btn{
  position:absolute; top:8px; right:8px; z-index:5;
  width:28px; height:28px; border:0; border-radius:6px;
  background: rgba(255,255,255,.95);
  box-shadow: 0 1px 2px rgba(0,0,0,.15);
  display:flex; align-items:center; justify-content:center;
  cursor:pointer; transition: transform .12s ease;
}
.next-copy-btn:hover{ transform: translateY(-1px); }

/* 图标（内置 SVG） */
.next-copy-btn svg{ width:16px; height:16px; fill:#333; }

/* 提示气泡 */
.next-copy-tip{
  position:absolute; top:-28px; right:8px; z-index:6;
  background:#333; color:#fff; font-size:12px; line-height:20px;
  padding:0 8px; border-radius:6px; white-space:nowrap;
  opacity:0; transform:translateY(6px); pointer-events:none;
  transition: all .18s ease;
}
.next-copy-tip.show{ opacity:1; transform:translateY(0); }
</style>


<script>
(function(){
  // 复用 NexT 的选择器习惯：figure.highlight 或 pre > code.language-*
  function codeBlocks(){
    const a = Array.from(document.querySelectorAll('figure.highlight'));
    const b = Array.from(document.querySelectorAll('pre > code[class*="language-"], pre > code[class^="language-"]'))
      .map(code => code.parentElement);
    // 去重
    return Array.from(new Set(a.concat(b)));
  }

  function makeIcon(){
    // NexT 默认用 Font Awesome 的剪贴板，这里内嵌一个等价 SVG
    const svgNS = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(svgNS, 'svg');
    svg.setAttribute('viewBox','0 0 24 24');
    svg.innerHTML = '<path d="M16 4h-1.18C14.4 2.84 13.3 2 12 2s-2.4.84-2.82 2H8a2 2 0 0 0-2 2v11a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2zm-4-1c.55 0 1 .45 1 1h-2c0-.55.45-1 1-1zM8 6h8v11H8V6z"/>';
    return svg;
  }

  function showTip(container, text){
    let tip = container.querySelector('.next-copy-tip');
    if(!tip){
      tip = document.createElement('div');
      tip.className = 'next-copy-tip';
      container.appendChild(tip);
    }
    tip.textContent = text;
    // 触发显示动画
    requestAnimationFrame(()=>{ tip.classList.add('show'); });
    setTimeout(()=> tip.classList.remove('show'), 1200);
  }

  function mount(){
    codeBlocks().forEach(container=>{
      if(container.querySelector('.next-copy-btn')) return;

      const btn = document.createElement('button');
      btn.className = 'next-copy-btn';
      btn.type = 'button';
      btn.setAttribute('aria-label', 'Copy');

      btn.appendChild(makeIcon());
      btn.addEventListener('click', ()=>{
        // 取文本
        let text = '';
        if(container.matches('figure.highlight')){
          const code = container.querySelector('td.code pre, pre code, pre');
          text = code ? (code.textContent || '') : '';
        }else{
          const code = container.querySelector('code');
          text = code ? (code.textContent || '') : '';
        }

        const ok = ()=> showTip(container, 'Copied!');
        const fail = ()=> showTip(container, 'Failed');

        if(navigator.clipboard && window.isSecureContext){
          navigator.clipboard.writeText(text).then(ok, fail);
        }else{
          // HTTP 兼容
          const ta = document.createElement('textarea');
          ta.value = text; ta.style.position='fixed'; ta.style.opacity='0';
          document.body.appendChild(ta); ta.focus(); ta.select();
          try{ document.execCommand('copy') ? ok() : fail(); }
          catch(e){ fail(); }
          document.body.removeChild(ta);
        }
      });

      container.appendChild(btn);
    });
  }

  if(document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', mount);
  else
    mount();

  // 如果之后启用 PJAX，解除注释
  // document.addEventListener('pjax:success', mount);
})();
</script>



<body itemscope itemtype="http://schema.org/WebPage">
  <div class="container use-motion">
    <div class="headband"></div>

    <!-- ====== Header ====== -->
    <header class="header" itemscope itemtype="http://schema.org/WPHeader">
      <div class="header-inner">
        <div class="site-brand-container">
          <div class="site-nav-toggle">
            <div class="toggle" aria-label="切换导航栏">
              <span class="toggle-line toggle-line-first"></span>
              <span class="toggle-line toggle-line-middle"></span>
              <span class="toggle-line toggle-line-last"></span>
            </div>
          </div>

          <div class="site-meta">
            <a href="/" class="brand" rel="start">
              <span class="logo-line-before"><i></i></span>
              <h1 class="site-title">{{SITENAME}}</h1>
              <span class="logo-line-after"><i></i></span>
            </a>
             <p class="site-subtitle" itemprop="description">{{SITE_SUBTITLE}}</p>
          </div>

          <div class="site-nav-right"><div class="toggle popup-trigger"></div></div>
        </div>

        <nav class="site-nav">
          <ul id="menu" class="main-menu menu">
            <li class="menu-item menu-item-home"><a href="/"><i class="fa fa-home fa-fw"></i>首页</a></li>
            <li class="menu-item menu-item-about"><a href="/about/"><i class="fa fa-user fa-fw"></i>关于</a></li>
            <li class="menu-item menu-item-tags"><a href="/tags/"><i class="fa fa-tags fa-fw"></i>标签</a></li>
            <li class="menu-item menu-item-categories"><a href="/categories/"><i class="fa fa-th fa-fw"></i>分类</a></li>
            <li class="menu-item menu-item-archives"><a href="/archives/"><i class="fa fa-archive fa-fw"></i>归档</a></li>
          </ul>
        </nav>
      </div>
    </header>

    <div class="back-to-top"><i class="fa fa-arrow-up"></i><span>0%</span></div>

    <!-- ====== Main ====== -->
	<main class="main">
	  <div class="main-inner">
		<!-- 内容区：居中限宽 -->
	<div class="content-wrap">
	  <div class="content post posts-expand">

		<!-- 文章块 -->
		<article itemscope itemtype="http://schema.org/Article" class="post-block" lang="zh-CN">
		  <link itemprop="mainEntityOfPage" href="{{PERMALINK}}">

		<header class="post-header">
			<h1 class="post-title" itemprop="name headline">{{TITLE}}</h1>
			<div class="post-meta">
			<span class="post-meta-item">
				<i class="far fa-calendar"></i>
				<span class="post-meta-item-text">发表于</span>
				<time title="创建时间：{{DATE_TEXT}}"
					datetime="{{DATE_PUBLISHED}}"
					itemprop="dateCreated datePublished">{{DATE_TEXT}}</time>
			</span>
			<span class="post-meta-item" style="{{DATE_UPDATED_TEXT_HIDE}}">
				<i class="far fa-calendar-check"></i>
				<span class="post-meta-item-text">更新于</span>
				<time title="修改时间：{{DATE_UPDATED_TEXT}}"
					datetime="{{DATE_UPDATED}}"
					itemprop="dateModified">{{DATE_UPDATED_TEXT}}</time>
			</span>
			</div>
		</header>




		  <!-- ✅ 关键正文 -->
		  <div class="post-body" itemprop="articleBody">
			{{CONTENT}}
		  </div>

		  <footer class="post-footer">
			<div class="post-tags">
			  {{TAGS_HTML}}
			</div>
			<div class="post-categories">
			  {{CATEGORIES_HTML}}
			</div>
		  </footer>
		</article>
		<!-- /文章块 -->

	  </div>
	</div>


    <!-- ✅ 侧栏必须是 content-wrap 的“同级兄弟”，而不是它的孩子 -->
    <aside class="sidebar">
      <div class="sidebar-inner">
        <ul class="sidebar-nav motion-element">
          <li class="sidebar-nav-toc">文章目录</li>
          <li class="sidebar-nav-overview">站点概览</li>
        </ul>

        <div class="post-toc-wrap sidebar-panel"></div>

        <div class="site-overview-wrap sidebar-panel">
          <div class="site-author motion-element" itemprop="author" itemscope itemtype="http://schema.org/Person">
            <p class="site-author-name" itemprop="name">{{AUTHOR_NAME}}</p>
            <div class="site-description" itemprop="description">{{AUTHOR_DESC}}</div>
          </div>

          <div class="site-state-wrap motion-element">
            <nav class="site-state">
              <div class="site-state-item site-state-posts">
                <a href="/archives/"><span class="site-state-item-count">{{COUNT_POSTS}}</span><span class="site-state-item-name">日志</span></a>
              </div>
              <div class="site-state-item site-state-categories">
                <a href="/categories/"><span class="site-state-item-count">{{COUNT_CATEGORIES}}</span><span class="site-state-item-name">分类</span></a>
              </div>
              <div class="site-state-item site-state-tags">
                <a href="/tags/"><span class="site-state-item-count">{{COUNT_TAGS}}</span><span class="site-state-item-name">标签</span></a>
              </div>
            </nav>
          </div>

          <div class="links-of-author motion-element">
            {{SIDEBAR_LINKS_HTML}}
          </div>
        </div>
      </div>
    </aside>
  </div>
</main>

<!-- 这一层半透明遮罩通常在 .main 之后 -->
<div id="sidebar-dimmer"></div>


    <!-- ====== Footer ====== -->
    <footer class="footer">
      <div class="footer-inner">
        <div class="copyright">
          &copy; <span itemprop="copyrightYear">{{YEAR}}</span>
          <span class="with-love"><i class="fa fa-heart"></i></span>
          <span class="author" itemprop="copyrightHolder">{{AUTHOR_NAME}}</span>
        </div>
        <div class="powered-by">由 <a href="https://hexo.io/" class="theme-link" rel="noopener" target="_blank">Hexo</a> &amp;
          <a href="https://theme-next.org/" class="theme-link" rel="noopener" target="_blank">NexT.Gemini</a> 强力驱动</div>
      </div>
    </footer>
  </div>

  <!-- NexT scripts -->
  <script src="/lib/anime.min.js"></script>
  <script src="//cdn.jsdelivr.net/npm/jquery@3/dist/jquery.min.js"></script>
  <script src="//cdn.jsdelivr.net/gh/fancyapps/fancybox@3/dist/jquery.fancybox.min.js"></script>
  <script src="/lib/velocity/velocity.min.js"></script>
  <script src="/lib/velocity/velocity.ui.min.js"></script>
  <script src="/js/utils.js"></script>
  <script src="/js/motion.js"></script>
  <script src="/js/schemes/pisces.js"></script>
  <script src="/js/next-boot.js"></script>
</body>
</html>
