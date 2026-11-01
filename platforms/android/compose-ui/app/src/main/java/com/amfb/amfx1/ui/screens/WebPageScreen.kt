package com.amfb.amfx1.ui.screens

import android.annotation.SuppressLint
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.amfb.amfx1.config.AppConfig

// Shows one mixer page (the bridge's index.html, opened at its tab) in a web view, with a big Back button to the home screen.
@SuppressLint("SetJavaScriptEnabled")
@Composable
fun WebPageScreen(tabId: String, onBack: () -> Unit) {
    Column(modifier = Modifier.fillMaxSize().padding(8.dp)) {
        Button(onClick = onBack, modifier = Modifier.fillMaxWidth()) {
            Text("Back to home", fontSize = 18.sp)
        }
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { context ->
                WebView(context).apply {
                    settings.javaScriptEnabled = true
                    settings.domStorageEnabled = true
                    webViewClient = object : WebViewClient() {
                        // Open the tab once the page is ready: the page's own switchTab() (index.html) does the work.
                        override fun onPageFinished(view: WebView, url: String?) {
                            view.evaluateJavascript("if (window.switchTab) window.switchTab('$tabId')", null)
                        }
                    }
                    loadUrl(AppConfig.BRIDGE_URL)
                }
            },
        )
    }
}
